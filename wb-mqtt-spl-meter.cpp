#include <iostream>
#include <stdexcept>
#include <fstream>
#include <cstring>
#include <utility>
#include <memory>
#include <getopt.h>
#include <math.h>

#include "jsoncpp/json/json.h"
#include <mosquittopp.h>
#include <wbmqtt/utils.h>
#include <wbmqtt/mqtt_wrapper.h>

#include <alsa/asoundlib.h>

using namespace std;

class THandlerConfig
{
	public:
		THandlerConfig(const std::string fname);

		string DeviceName;
		string AlsaDevice;
		int SampleRate;
		int Period;
		double K;
};

THandlerConfig::THandlerConfig(const string fname)
{
	// Let's parse it
	Json::Value root;
	Json::Reader reader;

	if (fname.empty()) {
		throw invalid_argument("Please specify config file with -c option");
	}

	ifstream myfile (fname);

	bool parsedSuccess = reader.parse(myfile,
			root,
			false);
	if (not parsedSuccess) {
		throw runtime_error("Failed to parse JSON\n" + reader.getFormattedErrorMessages());
	}

	DeviceName = root["device_name"].asString();
	AlsaDevice = root["alsa_device"].asString();
	SampleRate = root["sample_rate"].asInt();
	Period = root["period"].asInt();
	K = root["k"].asDouble();
}

class TMQTTSplHandler : public TMQTTWrapper
{
	public:
		TMQTTSplHandler(const TMQTTSplHandler::TConfig& mqtt_config, const THandlerConfig& config);
		~TMQTTSplHandler();
		
		void OnConnect(int rc);
		void OnMessage(const struct mosquitto_message *) {};
		void OnSubscribe(int, int, const int *) {};

		void PublishSPL(const int value);

	private:
		THandlerConfig Config;
};

TMQTTSplHandler::TMQTTSplHandler(const TMQTTSplHandler::TConfig& mqtt_config, const THandlerConfig& config)
	: TMQTTWrapper(mqtt_config)
	, Config(config)
{
	Connect();
}

TMQTTSplHandler::~TMQTTSplHandler() {}

void TMQTTSplHandler::OnConnect(int rc)
{
	printf("Connected with code %d.\n", rc);
	if(rc != 0)
		return;

	/* Only attempt to Subscribe on a successful connect. */
	string prefix = string("/devices/") + MQTTConfig.Id + "/";

	// Meta
	Publish(NULL, prefix + "meta/name", Config.DeviceName, 0, true);

	string control_prefix = prefix + "controls/dB";
	Publish(NULL, control_prefix + "/meta/type", "dB", 0, true);
	Publish(NULL, control_prefix + "/meta/readonly", "1", 0, true);
}

void TMQTTSplHandler::PublishSPL(int dB)
{
	Publish(NULL, string("/devices/") + MQTTConfig.Id + "/controls/dB", to_string(dB), 0, true);
}



int main(int argc, char *argv[])
{
	int ret, c;
	snd_pcm_t *capture;

	TMQTTSplHandler::TConfig mqtt_config;
	mqtt_config.Host = "localhost";
	mqtt_config.Port = 1883;
	mqtt_config.Id = "wb-spl-meter";

	string config_fname;
	
	while ( (c = getopt(argc, argv, "c:h:p:")) != -1) {
		//~ int this_option_optind = optind ? optind : 1;
		switch (c) {
		case 'c':
			config_fname = optarg;
			break;
		case 'p':
			mqtt_config.Port = stoi(optarg);
			cout << "port " << mqtt_config.Port << endl;
			break;
		case 'h':
			mqtt_config.Host = optarg;
			cout << "host " << mqtt_config.Host << endl;
			break;
		case '?':
			break;
		default:
			fprintf(stderr, "?? Getopt returned character code 0%o ??\n", c);
		}
	}

	try {
		mosqpp::lib_init();
		THandlerConfig config (config_fname);

		std::shared_ptr<TMQTTSplHandler> mqtt_handler(new TMQTTSplHandler(mqtt_config, config));
	    mqtt_handler->Init();

	    ret = mqtt_handler->loop_start();
		if (ret != 0)
			throw runtime_error("Couldn't start mosquitto_loop_start: " + to_string(ret));

		snd_pcm_sframes_t frames;
		char tmp[16];

		// Open handle of capture
		ret = snd_pcm_open(&capture, config.AlsaDevice.c_str(), SND_PCM_STREAM_CAPTURE, 0);
		if (ret < 0)
			throw runtime_error("PCM open failed: " + string(snd_strerror(ret)));
	
		ret = snd_pcm_set_params(capture,
						SND_PCM_FORMAT_S16_LE,
						SND_PCM_ACCESS_RW_INTERLEAVED,
						1,
						config.SampleRate,
						1,
						config.Period*1000);
		if (ret < 0)
			throw runtime_error("PCM parameters setting failed: " + string(snd_strerror(ret)));
		
		/*
		 * Formula of dB is 20log((Sound Pressure)/P0)
		   Assume that (Sound Pressure/P0) = k * sample value (Linear!),
		   and by experiment, we found that k = 0.45255.
		*/
		double Pvalue = 0;
		int dB = 0;
		int64_t square_sum = 0;
	
		unsigned int i;
	
		size_t buf_size = config.SampleRate/1000*config.Period;
		int16_t *buffer = new int16_t[buf_size];
		// Capture
		while (1) {
			frames = snd_pcm_readi(capture, buffer, buf_size);
			if (frames < 0) {
				frames = snd_pcm_recover(capture, frames, 0);
			}
			if (frames < 0) {
				cerr << "snd_pcm_readi failed" << endl;
				continue;
			}
			if (frames > 0 && frames < (long)buf_size) {
				cerr << "Warning: short read (expected " << buf_size << ", got " << frames << ")" << endl;
				continue;
			}
	
			square_sum = 0;
			for(i = 0; i < buf_size; i++) {
				square_sum += (buffer[i] * buffer[i]);
			}
			Pvalue = sqrt(square_sum / buf_size) * config.K;
	
			dB = (int)20*log10(Pvalue);
	
			snprintf(tmp, 16, "%d", dB);
			mqtt_handler->PublishSPL(dB);
		}
		snd_pcm_close(capture);
		mosqpp::lib_cleanup();
	}
	catch (const exception& ex) {
		cerr << "Error: " << ex.what() << endl;
		return EXIT_FAILURE;
	}

	return 0;
}

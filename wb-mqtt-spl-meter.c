#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <mosquitto.h>

#define MQTT_PREFIX "/devices/spl-meter"

static char *host = "127.0.0.1";
static int port = 1883;

static char *device = "default";
static int sample_rate = 16000;
static int period = 500;

static snd_pcm_t *capture;
struct mosquitto *mosq;

static int16_t *buffer;
static size_t buf_size;

static int parse_options(int argc, char *argv[])
{
	int ret;
	char *endptr;

	while ((ret = getopt(argc, argv, "h:p:d:r:t:")) != -1) {
		switch (ret) {
			case 'h':
				host = optarg;
				break;

			case 'p':
				port = strtol(optarg, &endptr, 10);
				if (*endptr != 0) {
					fprintf(stderr, "'%s' is not a valid port number\n", optarg);
					return 1;
				}
				break;

			case 'd':
				device = optarg;
				break;

			case 'r':
				sample_rate = strtol(optarg, &endptr, 10);
				if (*endptr != 0) {
					fprintf(stderr, "'%s' is not a valid sample rate\n", optarg);
					return 1;
				}
				break;

			case 't':
				period = strtol(optarg, &endptr, 10);
				if (*endptr != 0) {
					fprintf(stderr, "'%s' is not a valid RMS period\n", optarg);
					return 1;
				}
				break;

			default:
				fprintf(stderr, "Unknown option: -%c\n", ret);
				return 1;
		}
	}

	return 0;
}

static int init_capture(void)
{
	int ret;
	// Open handle of capture
	if ((ret = snd_pcm_open(&capture, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		fprintf(stderr, "Error: PCM open failed: %s\n", snd_strerror(ret));
		return 1;
	}

	if ((ret = snd_pcm_set_params(capture,
					SND_PCM_FORMAT_S16_LE,
					SND_PCM_ACCESS_RW_INTERLEAVED,
					1,
					sample_rate,
					1,
					period*1000)) < 0) {
		fprintf(stderr, "Error: PCM parameters setting failed: %s\n", snd_strerror(ret));
		return 1;
	}

	return 0;
}

static int init_mosquitto(void)
{
	int ret;
	char *str;

	mosquitto_lib_init();

	mosq = mosquitto_new(NULL, true, NULL);
	if (!mosq) {
		switch (errno) {
			case ENOMEM:
				fprintf(stderr, "Error: Out of memory.\n");
				break;

			case EINVAL:
				fprintf(stderr, "Error: Invalid id and/or clean_session.\n");
				break;
		}

		mosquitto_lib_cleanup();
		return 1;
	}

	ret = mosquitto_connect(mosq, host, port, 5);
	if (ret != 0) {
		fprintf(stderr, "Error: Can't connect to MQTT broker\n");
		return ret;
	}

	str = "dB";
	mosquitto_publish(mosq, NULL, MQTT_PREFIX "/controls/dB/meta/type", strlen(str), str, 2, true);

	str = "Sound level meter";
	mosquitto_publish(mosq, NULL, MQTT_PREFIX "/meta/name", strlen(str), str, 2, true);

	mosquitto_loop_start(mosq);

	return 0;
}

int main_loop(void)
{
	snd_pcm_sframes_t frames;
	char tmp[16];

	/*
	 * Formula of dB is 20log((Sound Pressure)/P0)
	   Assume that (Sound Pressure/P0) = k * sample value (Linear!),
	   and by experiment, we found that k = 0.45255.
	*/
	double k = 0.45255;
	double Pvalue = 0;
	int dB = 0;
	int64_t square_sum = 0;

	unsigned int i;

	// Capture
	while (1) {
		frames = snd_pcm_readi(capture, buffer, buf_size);
		if (frames < 0) {
			frames = snd_pcm_recover(capture, frames, 0);
		}
		if (frames < 0) {
			fprintf(stderr, "snd_pcm_readi failed\n");
			continue;
		}
		if (frames > 0 && frames < (long)buf_size) {
			fprintf(stderr, "Warning: short read (expected %li, got %li)\n", (long)buf_size, frames);
			continue;
		}

		square_sum = 0;
		for(i = 0; i < buf_size; i++) {
			square_sum += (buffer[i] * buffer[i]);
		}
		Pvalue = sqrt(square_sum / buf_size) * k;

		dB = (int)20*log10(Pvalue);

		snprintf(tmp, 16, "%d", dB);
		mosquitto_publish(mosq, NULL, MQTT_PREFIX "/controls/dB", strlen(tmp), tmp, 2, true);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	ret = parse_options(argc, argv);
	if (ret != 0) {
		fprintf(stderr, "Usage: %s [-h host] [-p port] [-d device] [-r rate] [-t period]\n", argv[0]);
		fprintf(stderr, "    -h host        MQTT broker host [127.0.0.1]\n");
		fprintf(stderr, "    -p port        MQTT broker port [18883]\n");
		fprintf(stderr, "    -d device      ALSA capture device [default]\n");
		fprintf(stderr, "    -r rate        Sample rate in Hz [16000]\n");
		fprintf(stderr, "    -t period      RMS integrating period in milliseconds [500]\n");
		exit(EXIT_FAILURE);
	}

	buf_size = sample_rate/1000*period;
	buffer = calloc(buf_size, sizeof(buffer[0]));
	if (!buffer) {
		fprintf(stderr, "Can't allocate memory for buffer\n");
		exit(EXIT_FAILURE);
	}

	if (init_capture() != 0) {
		exit(EXIT_FAILURE);
	}

	if (init_mosquitto() != 0) {
		exit(EXIT_FAILURE);
	}

	printf("%s:%d, %s %dHz, %dms; buf_size=%d\n",
			host, port, device, sample_rate, period, buf_size);
	ret = main_loop();

	mosquitto_loop_stop(mosq, false);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	snd_pcm_close(capture);

	return ret;
}

#include<alsa/asoundlib.h>
#include<math.h>

static char *device = "default";	/* device name */

/* 
   Each sample is a 16-bit signed integer, which means 
   the value can vary from -32768 to 32767.

   Size of the sample buffer = 8*1024 = 8192, which takes 
   8*1024*2 = 16384 bytes.

   Sampling frequency = 48000Hz, in other words, sound
   card will record the sample value every 1/48000 second.
*/
short buffer[8*1024];			

/*
   The unit of sizeof() function is byte, so we have to 
   shift 1 bit to the right to calculate the real size of buffer.
*/
int buffer_size = sizeof(buffer)>>1;

/*
   Function for calculating the Root Mean Square of sample buffer.
   RMS can calculate an average amplitude of buffer.
*/
double rms(short *buffer)
{
	int i;
	long int square_sum = 0.0;
	for(i=0; i<buffer_size; i++)
		square_sum += (buffer[i] * buffer[i]);
	
	double result = sqrt(square_sum/buffer_size);
	return result;
}
	
int main(void)
{
	int err;
	unsigned int i;
	
	snd_pcm_t *handle_capture;	/* handle of capture */

	snd_pcm_sframes_t frames;
	
	// Open handle of capture
	if((err=snd_pcm_open(&handle_capture, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		printf("Capture open error: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}
	
	if((err = snd_pcm_set_params(handle_capture,
				     SND_PCM_FORMAT_S16_LE,
				     SND_PCM_ACCESS_RW_INTERLEAVED,
				     1,
				     48000,
				     1,
				     500000)) < 0) {	/* 0.5s */
		printf("Capture open error: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}
	printf("             ");
	fflush(stdout);
	
	/*
	 * Formula of dB is 20log((Sound Pressure)/P0)
	   Assume that (Sound Pressure/P0) = k * sample value (Linear!),	   
	   and by experiment, we found that k = 0.45255.
	*/
	double k = 0.45255;
	double Pvalue = 0;
	int dB = 0;
	int peak = 0;
	i=0;

	// Capture 
	while(i<50) {
		frames = snd_pcm_readi(handle_capture, buffer, buffer_size);
		if(frames < 0)
			frames = snd_pcm_recover(handle_capture, frames, 0);
		if(frames < 0) {
			printf("snd_pcm_readi failed: %s\n", snd_strerror(err));
		}
		if(frames > 0 && frames < (long)buffer_size)
			printf("Short read (expected %li, wrote %li)\n", (long)buffer_size, frames);

		Pvalue = rms(buffer) * k;
		
		dB = (int)20*log10(Pvalue);
		if(dB > peak)
			peak = dB;
		int j;	
		for(j=0; j<13; j++)
			printf("\b");
		fflush(stdout);
		printf("dB=%d,Peak=%d", dB, peak);
		fflush(stdout);
	}
	printf("\n");
	snd_pcm_close(handle_capture);
	return 0;
}

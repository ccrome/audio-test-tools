/*
 * Simple ramp tester for testing linux sound cards.
 * 
 * Copyright 2015 Signal Essence, LLC.  All Rights reserved.
 * 
 */


#include <stdio.h>
#include <portaudio.h>
#include <string.h>
#include <argp.h>
#include <stdlib.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <curses.h>
struct ramp_opts_t {
	int channels;          // for now, must be symmetric until we find a better way
	int input_device_index;
	int output_device_index;
	float runtime_seconds;  // How long to run for, in seconds.
	float sample_rate;
	float suggested_latency;
	int dont_check_channels;
	int blocksize; /* frames_per_buffer */
} ramp_opts_t;

struct ramp_t {
	short int ramp;
	short int ramp_record;
	long  int sync_errors;
	long  int channel_errors_last;
	long  int sync_errors_last;
	long  int channel_errors;
	long  int latency;
	short int *capture_data;
	short int *playback_data;
	int frames;
	int channels;
	int started;
	long int capture_frames;
	long int capture_callbacks;
	long int playback_frames;
	long int playback_callbacks;
	struct ramp_opts_t *ramp_opts;
};

struct ramp_private_t {
	int output_channels;
	int input_channels;
	// options
	int channels;
	void *(*init)   (struct ramp_opts_t *, unsigned long frames, int channels);
	void (*playback)(unsigned long frames, int channels,       unsigned short *playback_out, void *priv);
	void (*capture) (unsigned long frames, int channels, const unsigned short *capture_in  , void *priv);
	void (*report)  (void *priv);
	void (*free)    (void **priv);
	void *priv;
	struct ramp_t *ramp;
};


WINDOW *mainwin;
int message(int y, int x, const char *format, ...)
{
	static FILE *f = NULL;
	if (f == NULL)
		f = fopen("test.txt", "w");
	va_list args;

	char msg[500];
	va_start(args, format);
	vsnprintf(msg, 500, format, args);
	mvaddstr(y, x, msg);
	fprintf(f, "%d, %d, %s\n", y, x, msg);
	va_end(args);
}
int caught_ctrl_c = 0;
void my_handler(int s) {
	caught_ctrl_c = 1;
}


void *ramp_init(struct ramp_opts_t *opts, unsigned long frames, int channels)
{
	struct ramp_t *ramp = malloc(sizeof(struct ramp_t));
	memset(ramp, 0, sizeof(struct ramp_t));
	ramp->ramp = 0;
	ramp->sync_errors = -1;  // expect 1 sync error upon startup.
	ramp->channel_errors = 0; // should never get a channel error
	ramp->ramp_record = 0;  // keep track of the ramp on the input
	ramp->capture_data  = malloc(sizeof(*ramp->capture_data)*opts->blocksize*frames*channels);
	ramp->playback_data = malloc(sizeof(*ramp->capture_data)*opts->blocksize*frames*channels);
	ramp->ramp_opts = opts;
	return ramp;
}

void ramp_free(void **priv)
{
	
	if (*priv) {
		struct ramp_t *ramp = *priv;
		if (ramp->capture_data)
			free(ramp->capture_data);
		if (ramp->playback_data) 
			free(ramp->playback_data);
		free(ramp);
		*priv = 0;
	}
}

void print_last_frames(void *priv, int y, int x)
{
	struct ramp_t *p = (struct ramp_t *)priv;
	int i;
	char cap[500];
	char ply[500];
	char tmp[500];
	snprintf(cap, 500, "cap: ");
	for (i = 0; i < p->channels; i++) {
		snprintf(tmp, 500, "%04x,", (unsigned short int)p->capture_data[i]);
		strcat(cap, tmp);
	}
	snprintf(ply, 500, "ply: ");
	for (i = 0; i < p->channels; i++) {
		snprintf(tmp, 500, "%04x,", (unsigned short int)p->playback_data[i]);
		strcat(ply, tmp);
	}
	message(y, x, ply);
	message(y+1, x, cap);
}


void ramp_report(void *priv)
{
	struct ramp_t *p = (struct ramp_t *)priv;
	unsigned long frame;
	unsigned channel;
	unsigned short int *in = p->capture_data;
	unsigned short int *out = p->playback_data;
	int y = 6;
	int x = 3;
	message(y++, x, "Sync Errors    : %10d", p->sync_errors);
	if (!(p->ramp_opts->dont_check_channels))
		message(y++, x, "Channel Errors : %10d", p->channel_errors);
	message(y++, x, "latency        : %10d", p->latency);
	message(y++, x, "Frames played  : %10d", p->playback_frames);
	if ((p->sync_errors == 0) &&
	    (p->channel_errors == 0))
	{
		message(y++, x, "**********************");
		message(y++, x, "***Congratulations!***");
		message(y++, x, "**********************");
	} else {
		if ((p->sync_errors != p->sync_errors_last) ||
		    (p->channel_errors != p->channel_errors_last)) {
			message(y++, x, "ERROR ERROR ERROR ERROR");
			message(y++, x, "ERROR ERROR ERROR ERROR");
			print_last_frames(p, y, x);
			p->channel_errors_last = p->channel_errors;
			p->sync_errors_last = p->sync_errors;
		}
	}
}

void conditional_break()
{
	volatile int i;
	i = 3;
}
void conditional_debug()
{
	static int count = 10;
	if (count-- == 0) {
		conditional_break();
	}
}

void ramp_capture(unsigned long frames, int channels, const unsigned short *capture_in, void *priv)
{
	struct ramp_t *p = (struct ramp_t *)priv;
	unsigned long frame;
	unsigned channel;
	short int ramp_rec = p->ramp_record;
	memcpy(p->capture_data, capture_in, sizeof(*capture_in)*frames*channels);
	p->latency = (p->playback_data[0] - p->capture_data[0]) & 0xFFF;
	p->frames = frames;
	p->channels = channels;
	int started = p->started;
	p->capture_frames += frames;
	p->capture_callbacks ++;
	if(!p->started) {
		const unsigned short *cap = capture_in;
		// check to see if we are receiving data yet
		for (frame = 0; frame < frames; frame++) {
			for (channel = 0; channel < channels; channel++) {
				if (*cap++ != 0)
					started++;
			}
		}
		if (started > 10) { // we've got more than a few data
			p->started = 1;
		} 
		return; // don't process the first block of data
	}
	
	const unsigned short *cap = capture_in;
	unsigned int channel_shift;
	if (!p->ramp_opts->dont_check_channels) {
		channel_shift = 12;
	} else {
		channel_shift = 16;
	}
	unsigned ramp_mask    = ((1 << channel_shift)-1) & 0xFFFF;
	unsigned channel_mask = 0xFFFF & ~ramp_mask;
	for (frame = 0; frame < frames; frame++) {
		int in_ramp;
		for (channel = 0; channel < channels; channel++) {
			unsigned short int value = *cap++;
			int in_ch = (value & channel_mask) >> channel_shift;
			if (in_ch != (channel & (channel_mask >> channel_shift)))
				p->channel_errors ++;
			in_ramp = value & ramp_mask;
			if (in_ramp != (ramp_rec & ramp_mask)){
				p->sync_errors++;
				ramp_rec = in_ramp;
				conditional_debug();
			}
		}
		ramp_rec = in_ramp+1;
	}
	p->ramp_record = ramp_rec;
}

void ramp_playback(unsigned long frames, int channels, unsigned short *playback_out, void *priv)
{
	unsigned long frame;
	unsigned channel;
	struct ramp_t *p = (struct ramp_t *)priv;
	short int ramp = p->ramp;
	unsigned short *playback_out_p = playback_out;
	p->playback_frames += frames;
	p->playback_callbacks ++;
	unsigned channel_shift;
	if (!p->ramp_opts->dont_check_channels) {
		channel_shift = 12;
	} else {
		channel_shift = 16;
	}
	unsigned ramp_mask    = ((1 << channel_shift)-1) & 0xFFFF;
	unsigned channel_mask = 0xFFFF & ~ramp_mask;
	for (frame = 0; frame < frames; frame++) {
		for (channel = 0; channel < channels; channel++) {
			*playback_out_p++ = (ramp & ramp_mask) | ((channel << channel_shift) & channel_mask);
		}
		ramp++;
	}
	p->ramp = ramp;
	memcpy(p->playback_data, playback_out, sizeof(*playback_out)*frames*channels);
}
int callback (
	const void *input,
	void *output,
	unsigned long frameCount,
	const PaStreamCallbackTimeInfo *timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
	)
{
	const short int *in = (short int *)input;
	short int *out      = (short int *)output;
	struct ramp_private_t *p = (struct ramp_private_t *)userData;
	if (p->playback)
		p->playback(frameCount, p->output_channels, out, p->priv);
	else
		memset(out, 0, sizeof(*out)*frameCount*p->output_channels);
	if (p->capture)
		p->capture(frameCount, p->output_channels, in, p->priv);
	return 0;
}

error_t parse_opt (int key, char *arg, struct argp_state *state) {
	struct ramp_opts_t *opts = state->input;
	switch (key) {
	case 'i':
		opts->input_device_index = atoi(arg);
		break;
	case 'o':
		opts->output_device_index = atoi(arg);
		break;
	case 'c':
		opts->channels = atoi(arg);
		break;
	case 't':
		opts->runtime_seconds = atof(arg);
		break;
	case 'f':
		opts->sample_rate = atof(arg);
		break;
	case 'l':
		opts->suggested_latency = atof(arg);
		break;
	case 'b':
		opts->blocksize = atoi(arg);
		break;
	case 'n':
		opts->dont_check_channels = 1;
		break;
	case ARGP_KEY_ARG:
		argp_usage(state);
		exit(-1);
	case ARGP_KEY_END:
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

int parse_arguments(int argc, char *argv[], struct ramp_opts_t *p)
{
	memset(p, 0, sizeof(*p));
	p->channels = 2;
	p->runtime_seconds = 10;
	p->sample_rate = 48000;
	p->suggested_latency=0.001;
	p->blocksize = 160;
	char doc[] =
		"%s:  uses portaudio to generate and read a ramp signal and "
		"verify that the received audio is bit-perfect from what is sent."
		;
	const char *args_doc = "";
	struct argp_option options[] = {
		{"blocksize",     'b', "FRAMES", 0, "number of frames to be processed per block"},
		{"input-device",  'i', "INPUT-DEVICE", 0, "input device index"},
		{"output-device", 'o', "OUTPUT-DEVICE", 0, "output device index"},
		{"channels",      'c', "CHANNELS", 0, "# channels for in and out"},
		{"runtime-seconds",'t',"SECONDS", 0, "how long to run"},
		{"sample-rate", 'f', "HZ", 0, "sample rate, hz"},
		{"suggested-latency", 'l',"SECONDS", 0, "suggested buffer latency" },
		{"no-check-channels", 'n', 0, 0, "Don't verify the channel slots. "
		 "This should generally be safe, and if you have very long delays, "
		 "this will provide an accurate result for latency."},
		{0}
	};
	struct argp argp = { options, parse_opt, args_doc, doc};
	argp_parse(&argp, argc, argv, 0, 0, p);
	return 0;
}

void set_ctrl_c_handler()
{
	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = my_handler;

	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);
}
int main(int argc, char *argv[])
{
	PaStreamFlags stream_flags = paNoFlag;
	PaError err;
	struct ramp_private_t priv;
	struct ramp_opts_t    args;
	struct ramp_t ramp;
	memset(&priv, 0, sizeof(priv));
	memset(&args, 0, sizeof(args));
	memset(&ramp, 0, sizeof(ramp));
	parse_arguments(argc, argv, &args);

	set_ctrl_c_handler();
	priv.init = ramp_init;
	priv.free = ramp_free;
	priv.playback = ramp_playback;
	priv.capture = ramp_capture;
	priv.report = ramp_report;
	priv.input_channels = args.channels;
	priv.output_channels = args.channels;
	priv.ramp = &ramp;
	err = Pa_Initialize();
	if (err != paNoError) goto error;
	PaStreamParameters ip = {
		.device = args.input_device_index,
		.channelCount = priv.input_channels,
		.sampleFormat = paInt16,
		.suggestedLatency = args.suggested_latency
	};
	PaStreamParameters op = {
		.device = args.output_device_index,
		.channelCount = priv.output_channels,
		.sampleFormat = paInt16,
		.suggestedLatency = args.suggested_latency
	};
	PaStream *stream;
	priv.priv = priv.init(&args, args.blocksize, args.channels);
	err = Pa_OpenStream(&stream, &ip, &op, args.sample_rate,
			    args.blocksize, stream_flags,
			    callback, &priv);
	if (err != paNoError) goto error;
	
	Pa_Sleep(1*500);
	Pa_StartStream(stream);
	int i;
	float runtime;
	if ((mainwin = initscr()) == NULL) {
		fprintf(stderr, "Error initializing ncurses.\n");
		exit(-1);
	}
	start_color();
	mvaddstr(1, 4, "Ramp Tester");
	refresh();
	long int expected_frames = 0;
	int update_rate_ms = 100;
	for (runtime = args.runtime_seconds * 1000;
	     runtime > update_rate_ms;
	     runtime -= update_rate_ms) {
		Pa_Sleep(1*update_rate_ms);
		if (caught_ctrl_c) {
			message(2, 4, "Caught a ctrl-c.  Quitting...");
			refresh();
			break;
		}
		priv.report(priv.priv);
		expected_frames += (float)args.sample_rate * update_rate_ms / 1000;
		message(2, 3, "Expected frames: %10d", expected_frames);
		refresh();
	}
	refresh();
	if (!caught_ctrl_c)
		Pa_Sleep(runtime);
	
	Pa_StopStream(stream);
	Pa_Terminate();
	delwin(mainwin);
	endwin();
	refresh();
	priv.report(priv.priv);
	priv.free(&priv.priv);
	long int frames_to_play = args.runtime_seconds;
	frames_to_play *= args.sample_rate;
	printf("Expected close to %d frames to be played\n",frames_to_play);
	return 0;
error:
	printf("PortAudio error: %s\n", Pa_GetErrorText(err));
	Pa_Terminate();
	return -1;
}

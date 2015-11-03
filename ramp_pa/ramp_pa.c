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
typedef struct {
	int channels;          // for now, must be symmetric until we find a better way
	int input_device_index;
	int output_device_index;
	float runtime_seconds;  // How long to run for, in seconds.
	float sample_rate;
	float suggested_latency;
	int dont_check_channels;
} ramp_opts_t;


typedef struct {
	short int ramp;
	short int ramp_record;
	long  int sync_errors;
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
	
} ramp_t;

typedef struct {
	int output_channels;
	int input_channels;
	// options
	int channels;
	void *(*init)   (unsigned long frames, int channels);
	void (*playback)(unsigned long frames, int channels,       unsigned short *playback_out, void *priv);
	void (*capture) (unsigned long frames, int channels, const unsigned short *capture_in  , void *priv);
	void (*report)  (void *priv);
	void (*free)    (void **priv);
	void *priv;
} ramp_private_t;


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
void *ramp_init(unsigned long frames, int channels)
{
	ramp_t *ramp = malloc(sizeof(ramp_t));
	memset(ramp, 0, sizeof(ramp_t));
	ramp->ramp = 0;
	ramp->sync_errors = -1;  // expect 1 sync error upon startup.
	ramp->channel_errors = 0; // should never get a channel error
	ramp->ramp_record = 0;  // keep track of the ramp on the input
	ramp->capture_data  = malloc(sizeof(*ramp->capture_data)*160*frames*channels);
	ramp->playback_data = malloc(sizeof(*ramp->capture_data)*160*frames*channels);
	return ramp;
}

void ramp_free(void **priv)
{
	
	if (*priv) {
		ramp_t *ramp = *priv;
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
	ramp_t *p = (ramp_t *)priv;
	int i;
	char cap[500];
	char ply[500];
	snprintf(cap, 500, "cap: ");
	for (i = 0; i < p->channels; i++)
		snprintf(cap, 500, "%s,0x%04x", cap, (unsigned short int)p->capture_data[i]);
	snprintf(cap, 500, "%s", cap);
	snprintf(ply, 500, "ply: ");
	for (i = 0; i < p->channels; i++)
		snprintf(ply, 500, "%s,0x%04x", ply, (unsigned short int)p->playback_data[i]);
	snprintf(ply, 500, "%s", ply);
	message(y, x, cap);
	message(y+1, x, ply);
}


void ramp_report_with_channel(void *priv)
{
	ramp_t *p = (ramp_t *)priv;
	unsigned long frame;
	unsigned channel;
	unsigned short int *in = p->capture_data;
	unsigned short int *out = p->playback_data;
	int fp = (p->playback_data[0]);
	int fc = (p->capture_data[0]);
	unsigned latency = ((fp - fc) & 0xFFF);
	int row = 6;
	int col = 3;
	message(row++, col, "Sync Errors    : %10d", p->sync_errors);
	message(row++, col, "Channel Errors : %10d", p->channel_errors);
	message(row++, col, "latency        : %10d", latency);
	message(row++, col, "Frames played  : %10d", p->playback_frames);
	message(row++, col, "Frames captured: %10d", p->capture_frames);
	if ((p->sync_errors == 0) &&
	    (p->channel_errors == 0))
	{
		message(row++, col, "**********************");
		message(row++, col, "***Congratulations!***");
		message(row++, col, "**********************");
	} else {
		message(row++, col, "ERROR ERROR ERROR ERROR");
		message(row++, col, "ERROR ERROR ERROR ERROR");
		print_last_frames(p, row, col);
	}
}
void ramp_report_with_latency(void *priv)
{
	ramp_t *p = (ramp_t *)priv;
	unsigned long frame;
	unsigned channel;
	unsigned short int *in = p->capture_data;
	unsigned short int *out = p->playback_data;
	int fp = (p->playback_data[0]);
	int fc = (p->capture_data[0]);
	unsigned latency = ((fp - fc) & 0xFFFF);
	int row = 6;
	int col = 3;
	message(row++, col,  "Sync Errors    : %10d", p->sync_errors);
	message(row++, col,  "Channel Errors : %10d", p->channel_errors);
	message(row++, col,  "latency        : %10d", latency);
	message(row++, col, "Frames played  : %10d", p->playback_frames);
	message(row++, col, "Frames captured: %10d", p->capture_frames);
	if ((p->sync_errors == 0) &&
	    (p->channel_errors == 0))
	{
		message(row++, col, "**********************");
		message(row++, col, "***Congratulations!***");
		message(row++, col, "**********************");
	} else {
		print_last_frames(p, row,  col);
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

void ramp_capture_with_channel(unsigned long frames, int channels, const unsigned short *capture_in, void *priv)
{
	ramp_t *p = (ramp_t *)priv;
	unsigned long frame;
	unsigned channel;
	short int ramp_rec = p->ramp_record;
	memcpy(p->capture_data, capture_in, sizeof(*capture_in)*frames*channels);
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
	for (frame = 0; frame < frames; frame++) {
		int in_ramp;
		for (channel = 0; channel < channels; channel++) {
			unsigned short int value = *cap++;
			int in_ch = (value & 0xF000) >> 12;
			if (in_ch != channel)
				p->channel_errors ++;
			in_ramp = value & 0x0FFF;
			if (in_ramp != (ramp_rec & 0xFFF)){
				p->sync_errors++;
				ramp_rec = in_ramp;
				conditional_debug();
			}
		}
		ramp_rec = in_ramp+1;
	}
	p->ramp_record = ramp_rec;
}
void ramp_capture_without_channel(unsigned long frames, int channels, const unsigned short *capture_in, void *priv)
{
	ramp_t *p = (ramp_t *)priv;
	unsigned long frame;
	unsigned channel;
	short int ramp_rec = p->ramp_record;
	memcpy(p->capture_data, capture_in, sizeof(*capture_in)*frames*channels);
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
	for (frame = 0; frame < frames; frame++) {
		int in_ramp;
		for (channel = 0; channel < channels; channel++) {
			unsigned short int value = *cap++;
			in_ramp = value & 0xFFFF;
			if (in_ramp != (ramp_rec & 0xFFFF)){
				p->sync_errors++;
				ramp_rec = in_ramp;
				conditional_debug();
			}
		}
		ramp_rec = in_ramp+1;
	}
	p->ramp_record = ramp_rec;
}

void ramp_playback_with_channel(unsigned long frames, int channels, unsigned short *playback_out, void *priv)
{
	unsigned long frame;
	unsigned channel;
	ramp_t *p = (ramp_t *)priv;
	short int ramp = p->ramp;
	unsigned short *playback_out_p = playback_out;
	p->playback_frames += frames;
	p->playback_callbacks ++;
	for (frame = 0; frame < frames; frame++) {
		for (channel = 0; channel < channels; channel++) {
			*playback_out_p++ = (ramp & 0x0FFF) | (channel << 12);
		}
		ramp++;
	}
	p->ramp = ramp;
	memcpy(p->playback_data, playback_out, sizeof(*playback_out)*frames*channels);
}
void ramp_playback_without_channel(unsigned long frames, int channels, unsigned short *playback_out, void *priv)
{
	unsigned long frame;
	unsigned channel;
	ramp_t *p = (ramp_t *)priv;
	short int ramp = p->ramp;
	unsigned short *playback_out_p = playback_out;
	p->playback_frames += frames;
	p->playback_callbacks ++;
	for (frame = 0; frame < frames; frame++) {
		for (channel = 0; channel < channels; channel++) {
			*playback_out_p++ = (ramp & 0xFFFF);
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
	ramp_private_t *p = (ramp_private_t *)userData;
	if (p->playback)
		p->playback(frameCount, p->output_channels, out, p->priv);
	else
		memset(out, 0, sizeof(*out)*frameCount*p->output_channels);
	if (p->capture)
		p->capture(frameCount, p->output_channels, in, p->priv);
	return 0;
}

error_t parse_opt (int key, char *arg, struct argp_state *state) {
	ramp_opts_t *opts = state->input;
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

int parse_arguments(int argc, char *argv[], ramp_opts_t *p)
{
	memset(p, 0, sizeof(*p));
	p->channels = 2;
	p->runtime_seconds = 10;
	p->sample_rate = 48000;
	p->suggested_latency=0.001;
	char doc[] =
		"%s:  uses portaudio to generate and read a ramp signal and "
		"verify that the received audio is bit-perfect from what is sent."
		;
	const char *args_doc = "";
	struct argp_option options[] = {
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
	int frames_per_buffer = 160;
	PaStreamFlags stream_flags = paNoFlag;
	PaError err;
	ramp_private_t priv;
	ramp_opts_t    args;
	ramp_t ramp;
	parse_arguments(argc, argv, &args);

	set_ctrl_c_handler();
	
	priv.init = ramp_init;
	priv.free = ramp_free;
	if (args.dont_check_channels) {
		priv.playback = ramp_playback_without_channel;
		priv.capture = ramp_capture_without_channel;
		priv.report = ramp_report_with_latency;
	} else {
		priv.playback = ramp_playback_with_channel;
		priv.capture = ramp_capture_with_channel;
		priv.report = ramp_report_with_channel;
	}
	priv.input_channels = args.channels;
	priv.output_channels = args.channels;

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
	priv.priv = priv.init(frames_per_buffer, args.channels);
	err = Pa_OpenStream(&stream, &ip, &op, args.sample_rate,
			    frames_per_buffer, stream_flags,
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
	for (runtime = args.runtime_seconds * 1000;
	     runtime > 100;
	     runtime -= 100) {
		Pa_Sleep(1*100);
		if (caught_ctrl_c) {
			message(2, 4, "Caught a ctrl-c.  Quitting...");
			refresh();
			break;
		}
		priv.report(priv.priv);
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

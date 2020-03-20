// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Adrian Bonislawski <adrian.bonislawski@intel.com>

/*
 * Probes will extract data for several probe points in one stream
 * with extra headers. This app will read the resulting file,
 * strip the headers and create wave files for each extracted buffer.
 *
 * Usage to parse data and create wave files: ./sof-probes -p data.bin
 *
 */

#include <ipc/probe.h>
#include <sof/math/numbers.h>
#include "wave.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APP_NAME "sof-probes"

#define PACKET_MAX_SIZE	4096	/**< Size limit for probe data packet */
#define DATA_READ_LIMIT 1024	/**< Data limit for file read */
#define FILES_LIMIT	32	/**< Maximum num of probe output files */
#define FILE_PATH_LIMIT 128	/**< Path limit for probe output files */

struct wave_files {
	FILE *fd;
	uint32_t buffer_id;
	uint32_t size;
	struct wave header;
};

enum p_state {
	READY = 0,		/**< At this stage app is looking for a SYNC word */
	SYNC,			/**< SYNC received, copying data */
	CHECK			/**< Check crc and save packet if valid */
};

static uint32_t sample_rate[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
	48000, 64000, 88200, 96000, 128000, 176400, 192000
};

static void usage(void)
{
	fprintf(stdout, "Usage %s <option(s)> <buffer_id/file>\n\n", APP_NAME);
	fprintf(stdout, "%s:\t -p file\tParse extracted file\n\n", APP_NAME);
	fprintf(stdout, "%s:\t -h \t\tHelp, usage info\n", APP_NAME);
	exit(0);
}

int write_data(char *path, char *data)
{
	FILE *fd;

	fd = fopen(path, "w");
	if (!fd) {
		fprintf(stderr, "error: unable to open file %s, error %d\n",
			path, errno);
		return errno;
	}

	fprintf(fd, "%s", data);
	fclose(fd);

	return 0;
}

int get_buffer_file(struct wave_files *files, uint32_t buffer_id)
{
	int i;

	for (i = 0; i < FILES_LIMIT; i++) {
		if (files[i].buffer_id == buffer_id)
			return i;
	}
	return -1;
}

int init_wave(struct wave_files *files, uint32_t buffer_id, uint32_t format)
{
	char path[FILE_PATH_LIMIT];
	int i;

	i = get_buffer_file(files, 0);
	if (i == -1) {
		fprintf(stderr, "error: too many buffers\n");
		exit(0);
	}

	fprintf(stdout, "%s:\t Creating wave file for buffer id: %d\n",
		APP_NAME, buffer_id);

	sprintf(path, "buffer_%d.wav", buffer_id);

	files[i].fd = fopen(path, "wb");
	if (!files[i].fd) {
		fprintf(stderr, "error: unable to create file %s, error %d\n",
			path, errno);
		exit(0);
	}

	files[i].buffer_id = buffer_id;

	files[i].header.riff.chunk_id = HEADER_RIFF;
	files[i].header.riff.format = HEADER_WAVE;
	files[i].header.fmt.subchunk_id = HEADER_FMT;
	files[i].header.fmt.subchunk_size = 16;
	files[i].header.fmt.audio_format = 1;
	files[i].header.fmt.num_channels = ((format & PROBE_MASK_NB_CHANNELS) >> PROBE_SHIFT_NB_CHANNELS) + 1;
	files[i].header.fmt.sample_rate = sample_rate[(format & PROBE_MASK_SAMPLE_RATE) >> PROBE_SHIFT_SAMPLE_RATE];
	files[i].header.fmt.bits_per_sample = (((format & PROBE_MASK_CONTAINER_SIZE) >> PROBE_SHIFT_CONTAINER_SIZE) + 1) * 8;
	files[i].header.fmt.byte_rate = files[i].header.fmt.sample_rate *
					files[i].header.fmt.num_channels *
					files[i].header.fmt.bits_per_sample / 8;
	files[i].header.fmt.block_align = files[i].header.fmt.num_channels *
					  files[i].header.fmt.bits_per_sample / 8;
	files[i].header.data.subchunk_id = HEADER_DATA;

	fwrite(&files[i].header, sizeof(struct wave), 1, files[i].fd);

	return i;
}

void finalize_wave_files(struct wave_files *files)
{
	uint32_t i, chunk_size;

	/* fill the header at the beginning of each file */
	/* and close all opened files */
	/* check wave struct to understand the offsets */
	for (i = 0; i < FILES_LIMIT; i++) {
		if (files[i].fd) {
			chunk_size = files[i].size + sizeof(struct wave) -
				     offsetof(struct riff_chunk, format);

			fseek(files[i].fd, sizeof(uint32_t), SEEK_SET);
			fwrite(&chunk_size, sizeof(uint32_t), 1, files[i].fd);
			fseek(files[i].fd, sizeof(struct wave) -
			      offsetof(struct data_subchunk, subchunk_size),
			      SEEK_SET);
			fwrite(&files[i].size, sizeof(uint32_t), 1, files[i].fd);

			fclose(files[i].fd);
		}
	}
}

int validate_data_packet(struct probe_data_packet *data_packet)
{
	uint32_t received_crc;
	uint32_t calc_crc;

	received_crc = data_packet->checksum;
	data_packet->checksum = 0;
	calc_crc = crc32(0, (char *)data_packet, sizeof(*data_packet));

	if (received_crc == calc_crc) {
		return 0;
	} else {
		fprintf(stderr, "error: data packet for buffer %d is not valid: crc32: %d/%d\n",
			data_packet->buffer_id, calc_crc, received_crc);
		return -EINVAL;
	}
}

void parse_data(char *file_in)
{
	FILE *fd_in;
	struct wave_files files[FILES_LIMIT];
	struct probe_data_packet *packet;
	uint32_t data[DATA_READ_LIMIT];
	uint32_t total_data_to_copy = 0;
	uint32_t data_to_copy = 0;
	uint32_t *w_ptr;
	int i, j, file;

	enum p_state state = READY;

	fprintf(stdout, "%s:\t Parsing file: %s\n", APP_NAME, file_in);

	fd_in = fopen(file_in, "rb");
	if (!fd_in) {
		fprintf(stderr, "error: unable to open file %s, error %d\n",
			file_in, errno);
		exit(0);
	}

	packet = malloc(PACKET_MAX_SIZE);
	if (!packet) {
		fprintf(stderr, "error: allocation failed, err %d\n",
			errno);
		fclose(fd_in);
		exit(0);
	}
	memset(&data, 0, sizeof(uint32_t) * DATA_READ_LIMIT);
	memset(&files, 0, sizeof(struct wave_files) * FILES_LIMIT);

	/* data read loop to process DATA_READ_LIMIT bytes at each iteration */
	do {
		i = fread(&data, sizeof(uint32_t), DATA_READ_LIMIT, fd_in);
		/* processing all loaded bytes */
		for (j = 0; j < i; j++) {
			/* SYNC received */
			if (data[j] == PROBE_EXTRACT_SYNC_WORD) {
				if (state != READY) {
					fprintf(stderr, "error: wrong state %d, err %d\n",
						state, errno);
					free(packet);
					exit(0);
				}
				memset(packet, 0, PACKET_MAX_SIZE);
				/* request to copy full data packet */
				total_data_to_copy = sizeof(struct probe_data_packet) /
					sizeof(uint32_t);
				/* probe_data_packet forced to align 4 */
				w_ptr = __builtin_assume_aligned((uint32_t *)packet, 4);
				state = SYNC;
			}
			/* data copying section */
			if (total_data_to_copy > 0) {
				/* check if there is enough bytes loaded */
				/* or copy partially if not */
				if (j + total_data_to_copy > i) {
					data_to_copy = i - j;
					total_data_to_copy -= data_to_copy;
				} else {
					data_to_copy = total_data_to_copy;
					total_data_to_copy = 0;
				}
				memcpy(w_ptr, data + j, data_to_copy * sizeof(uint32_t));
				w_ptr += data_to_copy;
				j += data_to_copy - 1;
			}

			if (total_data_to_copy == 0) {
				switch (state) {
				case READY:
					break;
				case SYNC:
					/* SYNC -> CHECK */
					/* request to copy data_size from probe packet */
					total_data_to_copy = packet->data_size_bytes /
							     sizeof(uint32_t);
					if (packet->data_size_bytes > PACKET_MAX_SIZE)
						packet = realloc(packet,
								 sizeof(struct probe_data_packet) +
								 packet->data_size_bytes);
					/* probe_data_packet forced to align 4 */
					w_ptr = __builtin_assume_aligned((uint32_t *)&packet->data, 4);
					state = CHECK;
					break;
				case CHECK:
					/* CHECK -> READY */
					/* find corresponding file and save data if valid */
					if (validate_data_packet(packet) == 0) {
						file = get_buffer_file(files,
								       packet->buffer_id);

						if (file < 0)
							file = init_wave(files,
									 packet->buffer_id,
									 packet->format);

						fwrite(packet->data,
						       sizeof(uint32_t),
						       packet->data_size_bytes /
						       sizeof(uint32_t),
						       files[file].fd);

						files[file].size += packet->data_size_bytes;
					}
					state = READY;
					break;
				}
			}
		}
	} while (i > 0);

	/* all done, can close files */
	finalize_wave_files(files);
	free(packet);
	fclose(fd_in);
	fprintf(stdout, "%s:\t done\n", APP_NAME);
}

int main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hp:")) != -1) {
		switch (opt) {
		case 'p':
			parse_data(optarg);
			break;
		case 'h':
		default:
			usage();
		}
	}

	return 0;
}

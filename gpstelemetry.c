/*
command-line tool to extract GPS time and position telemetry from GoPro videos
Copyright (C) 2021 Peter Lawrence

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "./gpmf-parser/GPMF_parser.h"
#include "./gpmf-parser/demo/GPMF_mp4reader.h"
#include "./gpmf-parser/GPMF_utils.h"

static const char *const column_names[] =
{
	"cts",
	"date",
	"GPS (Lat.) [deg]",
	"GPS (Long.) [deg]",
	"GPS (Alt.) [m]",
	"GPS (2D speed) [m/s]",
	"GPS (3D speed) [m/s]",
	"fix","precision",
};

int main(int argc, char* argv[])
{
	GPMF_ERR ret = GPMF_OK;
	GPMF_stream metadata_stream, *ms = &metadata_stream;
	double file_start = 0.0;

	if (argc < 2)
	{
		fprintf(stderr, "%s <mp4file> [mp4file_2] ... [mp4file_n]\n", argv[0]);
		return -1;
	}

	for (int argc_index = 1; argc_index < argc; argc_index++)
	{
		char *mp4filename = argv[argc_index];

		/* search for GPMF Track */
		size_t mp4handle = OpenMP4Source(mp4filename, MOV_GPMF_TRAK_TYPE, MOV_GPMF_TRAK_SUBTYPE, 0);

		if (mp4handle == 0)
		{
			fprintf(stderr, "ERROR: %s is an invalid MP4/MOV or it has no GPMF data\n\n", mp4filename);
			return -1;
		}

		double metadataduration = GetDuration(mp4handle);
		if (metadataduration <= 0.0) return -1;

		if (1 == argc_index)
		{
			/* print column names on the first row */
			for (int i = 0; i < (sizeof(column_names) / sizeof(*column_names)); i++)
				printf("%s\"%s\"", (i) ? "," : "", column_names[i]);
			printf("\n");
		}

		size_t payloadres = 0;
		double file_finish = 0.0;

		/* each MP4 has a given number of payloads, and we must iterate through all of them */
		uint32_t payloads = GetNumberPayloads(mp4handle);

		for (uint32_t index = 0; index < payloads; index++)
		{
			double start = 0.0, finish = 0.0;
			uint32_t payloadsize = GetPayloadSize(mp4handle, index);
			payloadres = GetPayloadResource(mp4handle, payloadres, payloadsize);

			uint32_t *payload = GetPayload(mp4handle, payloadres, index);
			if (payload == NULL) break;

			ret = GetPayloadTime(mp4handle, index, &start, &finish);
			if (ret != GPMF_OK) break;

			ret = GPMF_Init(ms, payload, payloadsize);
			if (ret != GPMF_OK) break;

			uint32_t fix;       /* data from "GPSF" */
			uint16_t precision; /* data from "GPSP" */
			struct              /* data from "GPSU" */
			{
				time_t time; /* second-accurate standard format compatible with time.h routines */
				double milliseconds; /* sub-second quantity to add to the above time_t data */
			} gpsu;

			/* iterate through all GPMF data in this particular payload */
			do
			{
				uint32_t key = GPMF_Key(ms);
				uint32_t samples = GPMF_Repeat(ms);
				uint32_t elements = GPMF_ElementsInStruct(ms);

				if (!samples) continue;

				uint32_t structsize = GPMF_StructSize(ms);

				if (!structsize) continue;

				uint32_t buffersize = samples * elements * structsize;
				void *tmpbuffer = malloc(buffersize);

				if (!tmpbuffer) continue;

				int res = GPMF_ERROR_UNKNOWN_TYPE;
				if ( (STR2FOURCC("GPSU") == key) || (STR2FOURCC("GPSF") == key) | (STR2FOURCC("GPSP") == key) )
					res = GPMF_FormattedData(ms, tmpbuffer, buffersize, 0, samples);
				else if (STR2FOURCC("GPS5") == key)
					res = GPMF_ScaledData(ms, tmpbuffer, buffersize, 0, samples, GPMF_TYPE_DOUBLE);

				if (GPMF_OK == res)
				{
					double *ptr = tmpbuffer;
					double step = (finish - start) / (double)samples;;
					double now = start;

					file_finish = finish;

					for (uint32_t i = 0; i < samples; i++)
					{
						if (STR2FOURCC("GPSU") == key)
						{
							char *gpsu_string = tmpbuffer;
							struct tm tm;
							memset(&tm, 0, sizeof(tm));

							/* GoPro provides the time as a fixed-size ASCII string, which we must convert to something useable */
							tm.tm_year  = 10 * (gpsu_string[0]  - '0') + (gpsu_string[1]  - '0');
							tm.tm_year += 100;
							tm.tm_mon   = 10 * (gpsu_string[2]  - '0') + (gpsu_string[3]  - '0');
							tm.tm_mon--; /* struct tm uses an ordinal month */
							tm.tm_mday  = 10 * (gpsu_string[4]  - '0') + (gpsu_string[5]  - '0');
							tm.tm_hour  = 10 * (gpsu_string[6]  - '0') + (gpsu_string[7]  - '0');
							tm.tm_min   = 10 * (gpsu_string[8]  - '0') + (gpsu_string[9]  - '0');
							tm.tm_sec   = 10 * (gpsu_string[10] - '0') + (gpsu_string[11] - '0');
							gpsu.time         = timegm(&tm);
							gpsu.milliseconds = 100.0 * (gpsu_string[13] - '0') + 10.0 * (gpsu_string[14] - '0') + (gpsu_string[15] - '0');
						}
						else if (STR2FOURCC("GPS5") == key)
						{
							/* at this point, we should have all the data (with "GPS5" being at the highest sample rate) */

							/* we print the time... */
							printf("%f, ", (file_start + now) * 1000.0);
							char ftimestr[64];
							strftime(ftimestr, sizeof(ftimestr), "%Y-%m-%dT%H:%M:%S", gmtime(&gpsu.time));
							printf("%s.%03dZ, ", ftimestr, (int)gpsu.milliseconds);

							/* ... and print out all the data */
							for (uint32_t j = 0; j < elements; j++)
								printf("%.6f, ", *ptr++);
							printf("%d, %d\n", fix, precision);

							/*
							the time increment potentially rolls over into the next minute, hour, or even day
							storing the second data in time_t makes our job much easier as strftime() above handles this
							*/
							now += step; gpsu.milliseconds += step * 1000.0;
							if (gpsu.milliseconds >= 1000.0)
							{
								gpsu.milliseconds -= 1000.0;
								gpsu.time++;
							}
						}
						else if (STR2FOURCC("GPSF") == key)
						{
							fix = *(uint32_t *)tmpbuffer;
						}
						else if (STR2FOURCC("GPSP") == key)
						{
							precision = *(uint16_t *)tmpbuffer;
						}
					}
				}

				free(tmpbuffer);

			} while (GPMF_OK == GPMF_Next(ms, GPMF_RECURSE_LEVELS));

			GPMF_ResetState(ms);
		}

		if (payloadres) FreePayloadResource(mp4handle, payloadres);
		if (ms) GPMF_Free(ms);
		CloseSource(mp4handle);

		if (ret != GPMF_OK)
		{
			if (GPMF_ERROR_UNKNOWN_TYPE == ret)
				fprintf(stderr, "ERROR: Unknown GPMF Type within\n");
			else
				fprintf(stderr, "ERROR: GPMF data has corruption\n");
			break;
		}

		file_start += file_finish;
	}

	return (int)ret;
}

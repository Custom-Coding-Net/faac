/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2002 Krzysztof Nikiel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: input.c,v 1.7 2003/07/10 19:18:38 knik Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef WIN32
#include <fcntl.h>
#endif

#include "input.h"

#define SWAP32(x) (((x & 0xff) << 24) | ((x & 0xff00) << 8) \
	| ((x & 0xff0000) >> 8) | ((x & 0xff000000) >> 24))
#define SWAP16(x) (((x & 0xff) << 8) | ((x & 0xff00) >> 8))

#ifdef WORDS_BIGENDIAN
# define UINT32(x) SWAP32(x)
# define UINT16(x) SWAP16(x)
#else
# define UINT32(x) (x)
# define UINT16(x) (x)
#endif

typedef struct
{
  u_int32_t label;           /* 'RIFF' */
  u_int32_t length;        /* Length of rest of file */
  u_int32_t chunk_type;      /* 'WAVE' */
}
riff_t;

typedef struct
{
  u_int32_t label;
  u_int32_t len;
}
riffsub_t;

#define WAVE_FORMAT_PCM		1
#define WAVE_FORMAT_EXTENSIBLE	0xfffe
typedef struct
{
  u_int16_t wFormatTag;
  u_int16_t nChannels;
  u_int32_t nSamplesPerSec;
  u_int32_t nAvgBytesPerSec;
  u_int16_t nBlockAlign;
  u_int16_t wBitsPerSample;
  u_int16_t cbSize;
}
WAVEFORMATEX;

typedef struct
{
  WAVEFORMATEX Format;
  union {
    u_int16_t wValidBitsPerSample;	// bits of precision
    u_int16_t wSamplesPerBlock;		// valid if wBitsPerSample==0
    u_int16_t wReserved;		// If neither applies, set to zero.
  } Samples;
  u_int32_t dwChannelMask;		// which channels are present in stream
  unsigned char SubFormat[16];		// guid
} WAVEFORMATEXTENSIBLE;

static unsigned char waveformat_pcm_guid[16] =
{
  WAVE_FORMAT_PCM,0,0,0,
  0x00, 0x00,
  0x10, 0x00,
  0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static void unsuperr(const char *name)
{
  fprintf(stderr, "%s: file format not supported\n", name);
}

pcmfile_t *wav_open_read(const char *name, int rawinput)
{
  int i;
  int skip;
  FILE *wave_f;
  riff_t riff;
  riffsub_t riffsub;
  WAVEFORMATEXTENSIBLE wave;
  char *riffl = "RIFF";
  char *wavel = "WAVE";
  char *fmtl = "fmt ";
  char *datal = "data";
  int fmtsize;
  pcmfile_t *sndf;
  int dostdin = 0;

  if (!strcmp(name, "-"))
  {
#ifdef WIN32
    setmode(fileno(stdin), O_BINARY);
#endif
    wave_f = stdin;
    dostdin = 1;
  }
  else if (!(wave_f = fopen(name, "rb")))
  {
    perror(name);
    return NULL;
  }

  if (!rawinput) // header input
  {
    if (fread(&riff, 1, sizeof(riff), wave_f) != sizeof(riff))
      return NULL;
    if (memcmp(&(riff.label), riffl, 4))
      return NULL;
    if (memcmp(&(riff.chunk_type), wavel, 4))
      return NULL;

    if (fread(&riffsub, 1, sizeof(riffsub), wave_f) != sizeof(riffsub))
      return NULL;
    riffsub.len = UINT32(riffsub.len);
    if (memcmp(&(riffsub.label), fmtl, 4))
      return NULL;
    memset(&wave, 0, sizeof(wave));
    fmtsize = (riffsub.len < sizeof(wave)) ? riffsub.len : sizeof(wave);
    if (fread(&wave, 1, fmtsize, wave_f) != fmtsize)
      return NULL;

    for (skip = riffsub.len - fmtsize; skip > 0; skip--)
      fgetc(wave_f);

    for (i = 0;; i++)
    {
      if (fread(&riffsub, 1, sizeof(riffsub), wave_f) != sizeof(riffsub))
	return NULL;
      riffsub.len = UINT32(riffsub.len);
      if (!memcmp(&(riffsub.label), datal, 4))
	break;
      if (i > 10)
	return NULL;

      for (skip = riffsub.len; skip > 0; skip--)
	fgetc(wave_f);
    }
    if (UINT16(wave.Format.wFormatTag) != WAVE_FORMAT_PCM)
    {
      if (UINT16(wave.Format.wFormatTag) == WAVE_FORMAT_EXTENSIBLE)
      {
	if (UINT16(wave.Format.cbSize) < 22) // struct too small
	  return NULL;
	if (memcmp(wave.SubFormat, waveformat_pcm_guid, 16))
	{
          unsuperr(name);
	  return NULL;
	}
      }
      else
      {
	unsuperr(name);
	return NULL;
      }
    }
  }

  sndf = malloc(sizeof(*sndf));
  memset(sndf, 0, sizeof(*sndf));
  sndf->f = wave_f;
  if (rawinput)
  {
    sndf->bigendian = 1;
    if (dostdin)
      sndf->samples = 0;
    else
    {
      fseek(sndf->f, 0 , SEEK_END);
      sndf->samples = ftell(sndf->f) / (sndf->samplebytes * sndf->channels);
      rewind(sndf->f);
    }
  }
  else
  {
    sndf->bigendian = 0;
    sndf->channels = UINT16(wave.Format.nChannels);
    sndf->samplebytes = UINT16(wave.Format.wBitsPerSample) / 8;
    sndf->samplerate = UINT32(wave.Format.nSamplesPerSec);
    sndf->samples = riffsub.len / (sndf->samplebytes * sndf->channels);
  }
  return sndf;
}


static void chan_remap(int32_t *buf, int channels, int blocks, int *map)
{
  int i;
  int32_t *tmp = alloca(channels * sizeof(int32_t));

  for (i = 0; i < blocks; i++)
  {
    int chn;

    memcpy(tmp, buf + i * channels, sizeof(int32_t) * channels);

    for (chn = 0; chn < channels; chn++)
      buf[i * channels + chn] = tmp[map[chn]];
  }
}

size_t wav_read_int24(pcmfile_t *sndf, int32_t *buf, size_t num, int *map)
{
  int size;
  int i;
  unsigned char *bufi;

  if ((sndf->samplebytes > 4) || (sndf->samplebytes < 1))
    return 0;

  bufi = (char *)buf + sizeof(*buf) * num - sndf->samplebytes * (num - 1) - sizeof(*buf);

  size = fread(bufi, sndf->samplebytes, num, sndf->f);

  // convert to 24 bit
  // fix endianness
  switch (sndf->samplebytes) {
  case 1:
    /* this is endian clean */
    for (i = 0; i < size; i++)
      buf[i] = (bufi[i] - 128) * 65536;
    break;

  case 2:
#ifdef WORDS_BIGENDIAN
    if (!sndf->bigendian)
#else
    if (sndf->bigendian)
#endif
    {
      // swap bytes
      for (i = 0; i < size; i++)
      {
	int16_t s = ((int16_t *)bufi)[i];

	s = SWAP16(s);

	buf[i] = ((uint32_t)s) << 8;
      }
    }
    else
    {
      // no swap
      for (i = 0; i < size; i++)
      {
	int s = ((int16_t *)bufi)[i];

	buf[i] = s << 8;
      }
    }
    break;

  case 3:
    if (!sndf->bigendian)
    {
      for (i = 0; i < size; i++)
      {
	int s = bufi[3 * i] | (bufi[3 * i + 1] << 8) | (bufi[3 * i + 2] << 16);

        // fix sign
	if (s & 0x800000)
          s |= 0xff000000;

	buf[i] = s;
      }
    }
    else // big endian input
    {
      for (i = 0; i < size; i++)
      {
	int s = (bufi[3 * i] << 16) | (bufi[3 * i + 1] << 8) | bufi[3 * i + 2];

        // fix sign
	if (s & 0x800000)
          s |= 0xff000000;

	buf[i] = s;
      }
    }
    break;

  case 4:
#ifdef WORDS_BIGENDIAN
    if (!sndf->bigendian)
#else
    if (sndf->bigendian)
#endif
    {
      // swap bytes
      for (i = 0; i < size; i++)
      {
	int s = buf[i];

	buf[i] = SWAP32(s);
      }
    }
    break;
  }

  if (map)
    chan_remap(buf, sndf->channels, size / sndf->channels, map);

  return size;
}

int wav_close(pcmfile_t *sndf)
{
  int i = fclose(sndf->f);
  free(sndf);
  return i;
}

/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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
 * $Id: faac.h,v 1.28 2003/07/10 19:16:01 knik Exp $
 */

#ifndef FAACLIB_H
#define FAACLIB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef WIN32
  #pragma pack(push, 1)
  #ifndef FAACAPI
    #define FAACAPI __stdcall
  #endif
#else
  #ifndef FAACAPI
    #define FAACAPI
  #endif
#endif

#define FAAC_CFG_VERSION 101

/* MPEG ID's */
#define MPEG2 1
#define MPEG4 0

/* AAC object types */
#define MAIN 0
#define LOW  1
#define SSR  2
#define LTP  3

typedef struct faacEncConfiguration
{
    /* config version */
    /*const*/ int version;

    /* library version */
    /*const*/ char *name;

    /* copyright string */
    char *copyright;

	/* MPEG version, 2 or 4 */
	unsigned int mpegVersion;

	/* AAC object type */
	unsigned int aacObjectType;

	/* Allow mid/side coding */
	unsigned int allowMidside;

	/* Use one of the channels as LFE channel */
	unsigned int useLfe;

	/* Use Temporal Noise Shaping */
	unsigned int useTns;

	/* bitrate / channel of AAC file */
	unsigned long bitRate;

	/* AAC file frequency bandwidth */
	unsigned int bandWidth;

    /* Quantizer quality */
    unsigned long quantqual;

	/*
		Bitstream output format, meaning:
		0 - Raw
		1 - ADTS
		/AV
	*/
	unsigned int outputFormat;

	// psychoacoustic model list
	/*const*/ struct {
	  void *ptr;
	  char *name;
	} *psymodellist;
	// selected index in psymodellist
	unsigned int psymodelidx;

} faacEncConfiguration, *faacEncConfigurationPtr;

typedef void *faacEncHandle;

faacEncConfigurationPtr FAACAPI faacEncGetCurrentConfiguration(faacEncHandle hEncoder);
int FAACAPI faacEncSetConfiguration (faacEncHandle hEncoder, faacEncConfigurationPtr config);

faacEncHandle FAACAPI faacEncOpen(unsigned long sampleRate,
								  unsigned int numChannels,
								  unsigned long *inputSamples,
								  unsigned long *maxOutputBytes);

int FAACAPI faacEncGetDecoderSpecificInfo(faacEncHandle hEncoder,
										  unsigned char** ppBuffer,
										  unsigned long* pSizeOfDecoderSpecificInfo);

int FAACAPI faacEncEncode(faacEncHandle hEncoder,
			  int32_t *inputBuffer,
			  unsigned int samplesInput,
			  unsigned char *outputBuffer,
			  unsigned int bufferSize
			 );

int FAACAPI faacEncClose(faacEncHandle hEncoder);


#ifdef WIN32
  #pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FAACLIB_H */
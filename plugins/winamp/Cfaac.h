/*
FAAC - codec plugin for Cooledit
Copyright (C) 2004 Antonio Foranna

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation.
	
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
		
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
			
The author can be contacted at:
ntnfrn_email-temp@yahoo.it
*/

#ifndef _Cfaac_H
#define _Cfaac_H

// *********************************************************************************************

#include <mp4.h>		// int32_t, ...
#include <faad.h>		// FAAD2 version
#ifdef MAIN
	#undef MAIN
#endif
#ifdef SSR
	#undef SSR
#endif
#ifdef LTP
	#undef LTP
#endif
#include <faac.h>
#include <win32_ver.h>	// mpeg4ip version
#include <id3/tag.h>	// id3 tag
#include "CRegistry.h"
#include "Defines.h"	// my defines

// *********************************************************************************************

#ifdef	ADTS
#undef	ADTS
#define ADTS 1
#endif

// *********************************************************************************************

#define REG_AUTO "Auto"
#define DEF_AUTO true
#define REG_WRITEMP4 "Write MP4"
#define DEF_WRITEMP4 false
#define REG_MPEGVER "MPEG version"
#define DEF_MPEGVER MPEG4
#define REG_PROFILE "Profile"
#define DEF_PROFILE LOW
#define REG_MIDSIDE "MidSide"
#define DEF_MIDSIDE true
#define REG_TNS "TNS"
#define DEF_TNS true
#define REG_LFE "LFE"
#define DEF_LFE false
#define REG_USEQUALTY "Use quality"
#define DEF_USEQUALTY false
#define REG_QUALITY "Quality"
#define DEF_QUALITY 100
#define REG_BITRATE "BitRate"
#define DEF_BITRATE 0 /* quality on */
#define REG_BANDWIDTH "BandWidth"
#define DEF_BANDWIDTH 0
#define REG_HEADER "Header"
#define DEF_HEADER ADTS

#define REG_TAGON "Tag On"
#define REG_ARTIST "Tag Artist"
#define REG_TITLE "Tag Title"
#define REG_ALBUM "Tag Album"
#define REG_YEAR "Tag Year"
#define REG_GENRE "Tag Genre"
#define REG_WRITER "Tag Writer"
#define REG_COMMENT "Tag Comment"
#define REG_TRACK "Tag Track"
#define REG_NTRACKS "Tag Tracks"
#define REG_DISK "Tag Disk"
#define REG_NDISKS "Tag Disks"
#define REG_COMPILATION "Tag Compilation"
#define REG_ARTFILE "Tag Art file"

// *********************************************************************************************

typedef struct
{
BYTE	On;
char	*artist, *title, *album, *year, *genre, *writer, *comment;
int		trackno,ntracks, discno,ndiscs;
BYTE	compilation;
char	*artFileName;
} MP4TAG;
// -----------------------------------------------------------------------------------------------

typedef struct mec
{
bool					AutoCfg,
						UseQuality,
						SaveMP4;
char					*OutDir;
faacEncConfiguration	EncCfg;
MP4TAG					Tag;
} MY_ENC_CFG;
// -----------------------------------------------------------------------------------------------

typedef struct output_tag  // any special vars associated with output file
{
// MP4
MP4FileHandle 	MP4File;
MP4TrackId		MP4track;
MP4Duration		TotalSamples,
				WrittenSamples,
				encoded_samples;
DWORD			frameSize,
				ofs;

// AAC
FILE			*aacFile;
char			*Filename;

// GLOBAL
long			Samprate;
WORD			BitsPerSample;
WORD			Channels;
DWORD			srcSize;
//char			*dst_name;		// name of compressed file

faacEncHandle	hEncoder;
int32_t			*buf32bit;
BYTE			*bufIn;
unsigned char	*bitbuf;
long			bytes_into_buffer;
DWORD			maxBytesOutput;
long			samplesInput,
				samplesInputSize;
bool			WriteMP4;
} MYOUTPUT;



// *********************************************************************************************



class Cfaac
{
private:
	virtual void DisplayError(char *ProcName, char *str);
	virtual HANDLE ERROR_Init(char *str) { DisplayError("Init", str); return NULL; }
	virtual int ERROR_processData(char *str) { DisplayError("processData", str); return -1; }
	virtual void showInfo(MYOUTPUT *mi) {}
	virtual void showProgress(MYOUTPUT *mi) {}
	void To32bit(int32_t *buf, BYTE *bufi, int size, BYTE samplebytes, BYTE bigendian);
	int check_image_header(const char *buf);
	int ReadCoverArtFile(char *pCoverArtFile, char **artBuf);

public:
    Cfaac(HANDLE hOutput=NULL);
    virtual ~Cfaac();

	static void FreeTag(MP4TAG *Tag);
	static void getFaacCfg(MY_ENC_CFG *cfg);
	static void setFaacCfg(MY_ENC_CFG *cfg);
	virtual void WriteMP4Tag(MP4FileHandle MP4File, MP4TAG *Tag);
	virtual void WriteAacTag(char *Filename, MP4TAG *Tag);
    virtual HANDLE Init(LPSTR lpstrFilename,long lSamprate,WORD wBitsPerSample,WORD wChannels,long FileSize);
    virtual int processData(HANDLE hOutput, BYTE *bufIn, DWORD len);
	virtual int processDataBufferized(HANDLE hOutput, BYTE *bufIn, long lBytes);
/*
// AAC
	bool            BlockSeeking;

// GLOBAL
	long            newpos_ms;
	BOOL            IsSeekable;
	MYINPUT			*mi;
*/
	HANDLE			hOutput;
};

#endif

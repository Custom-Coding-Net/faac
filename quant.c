#include <math.h>
#include <string.h>
#include "aacenc.h"
#include "quant.h"
#include "bitstream.h"
#include "tf_main.h"
#include "pulse.h"
#include "huffman.h"
#include "aac_se_enc.h"



double pow_quant[9000];
double adj_quant[9000];
double adj_quant_asm[9000];
int sign[1024];
int g_Count;
int old_startsf;
int pns_sfb_start = 1000;         /* lower border for Perceptual Noise Substitution
                                      (off by default) */

double ATH[SFB_NUM_MAX];

double ATHformula(double f)
{
	double ath;
	f  = max(0.02, f);
	/* from Painter & Spanias, 1997 */
	/* minimum: (i=77) 3.3kHz = -5db */
	ath=(3.640 * pow(f,-0.8)
		-  6.500 * exp(-0.6*pow(f-3.3,2.0))
		+  0.001 * pow(f,4.0));

	/* convert to energy */
	ath = pow( 10.0, ath/10.0 );
	return ath;
}


void compute_ath(AACQuantInfo *quantInfo, double ATH[SFB_NUM_MAX])
{
	int sfb,i,start=0,end=0;
	double ATH_f;
	double samp_freq = 44.1;
	static int width[] = {0, 4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 12, 16, 16, 16};
	if (quantInfo->block_type==ONLY_SHORT_WINDOW) {
		for ( sfb = 0; sfb < 14; sfb++ ) {
			start = start+(width[sfb]*8);
			end   = end+(width[sfb+1]*8);
			ATH[sfb]=1e99;
			for (i=start ; i < end; i++) {
				ATH_f = ATHformula(samp_freq*i/(128)); /* freq in kHz */
				ATH[sfb]=min(ATH[sfb],ATH_f);
			}
		}
	} else {
		for ( sfb = 0; sfb < quantInfo->nr_of_sfb; sfb++ ) {
			start = quantInfo->sfb_offset[sfb];
			end   = quantInfo->sfb_offset[sfb+1];
			ATH[sfb]=1e99;
			for (i=start ; i < end; i++) {
				ATH_f = ATHformula(samp_freq*i/(1024)); /* freq in kHz */
				ATH[sfb]=min(ATH[sfb],ATH_f);
			}
		}
	}
}


void tf_init_encode_spectrum_aac( int quality )
{
	int i;

	g_Count = quality;
	old_startsf = 0;

	for (i=0;i<9000;i++){
		pow_quant[i]=pow(i, ((double)4.0/(double)3.0));
	}
	for (i=0;i<8999;i++){
		adj_quant[i] = (i + 1) - pow(0.5 * (pow_quant[i] + pow_quant[i + 1]), 0.75);
	}

	adj_quant_asm[0] = 0.0;
	for (i = 1; i < 9000; i++) {
		adj_quant_asm[i] = i - 0.5 - pow(0.5 * (pow_quant[i - 1] + pow_quant[i]),0.75);
	}
}


#if (defined(__GNUC__) && defined(__i386__))
#define USE_GNUC_ASM
#endif

#ifdef USE_GNUC_ASM
#  define QUANTFAC(rx)  adj_quant_asm[rx]
#  define XRPOW_FTOI(src, dest) \
     asm ("fistpl %0 " : "=m"(dest) : "t"(src) : "st")
#else
#  define QUANTFAC(rx)  adj_quant[rx]
#  define XRPOW_FTOI(src,dest) ((dest) = (int)(src))
#endif

/*********************************************************************
 * nonlinear quantization of xr
 * More accurate formula than the ISO formula.  Takes into account
 * the fact that we are quantizing xr -> ix, but we want ix^4/3 to be
 * as close as possible to x^4/3.  (taking the nearest int would mean
 * ix is as close as possible to xr, which is different.)
 * From Segher Boessenkool <segher@eastsite.nl>  11/1999
 * ASM optimization from
 *    Mathew Hendry <scampi@dial.pipex.com> 11/1999
 *    Acy Stapp <AStapp@austin.rr.com> 11/1999
 *    Takehiro Tominaga <tominaga@isoternet.org> 11/1999
 *********************************************************************/
void quantize(AACQuantInfo *quantInfo,
			  double *pow_spectrum,
			  int *quant)
{
	const double istep = pow(2.0, -0.1875*quantInfo->common_scalefac);

#if ((defined _MSC_VER) || (defined __BORLANDC__))
	{
		/* asm from Acy Stapp <AStapp@austin.rr.com> */
		int rx[4];
		_asm {
			fld qword ptr [istep]
			mov esi, dword ptr [pow_spectrum]
			lea edi, dword ptr [adj_quant_asm]
			mov edx, dword ptr [quant]
			mov ecx, 1024/4
		}
loop1:
		_asm {
			fld qword ptr [esi]         // 0
			fld qword ptr [esi+8]       // 1 0
			fld qword ptr [esi+16]      // 2 1 0
			fld qword ptr [esi+24]      // 3 2 1 0


			fxch st(3)                  // 0 2 1 3
			fmul st(0), st(4)
			fxch st(2)                  // 1 2 0 3
			fmul st(0), st(4)
			fxch st(1)                  // 2 1 0 3
			fmul st(0), st(4)
			fxch st(3)                  // 3 1 0 2
			fmul st(0), st(4)
			add esi, 32
			add edx, 16

			fxch st(2)                  // 0 1 3 2
			fist dword ptr [rx]
			fxch st(1)                  // 1 0 3 2
			fist dword ptr [rx+4]
			fxch st(3)                  // 2 0 3 1
			fist dword ptr [rx+8]
			fxch st(2)                  // 3 0 2 1
			fist dword ptr [rx+12]

			dec ecx

			mov eax, dword ptr [rx]
			mov ebx, dword ptr [rx+4]
			fxch st(1)                  // 0 3 2 1
			fadd qword ptr [edi+eax*8]
			fxch st(3)                  // 1 3 2 0
			fadd qword ptr [edi+ebx*8]

			mov eax, dword ptr [rx+8]
			mov ebx, dword ptr [rx+12]
			fxch st(2)                  // 2 3 1 0
			fadd qword ptr [edi+eax*8]
			fxch st(1)                  // 3 2 1 0
			fadd qword ptr [edi+ebx*8]

			fxch st(3)                  // 0 2 1 3
			fistp dword ptr [edx-16]    // 2 1 3
			fxch st(1)                  // 1 2 3
			fistp dword ptr [edx-12]    // 2 3
			fistp dword ptr [edx-8]     // 3
			fistp dword ptr [edx-4]

			jnz loop1

			mov dword ptr [pow_spectrum], esi
			mov dword ptr [quant], edx
			fstp st(0)
		}
	}
#elif defined (USE_GNUC_ASM)
  {
      int rx[4];
      __asm__ __volatile__(
        "\n\nloop1:\n\t"

        "fldl (%1)\n\t"
        "fldl 8(%1)\n\t"
        "fldl 16(%1)\n\t"
        "fldl 24(%1)\n\t"

        "fxch %%st(3)\n\t"
        "fmul %%st(4)\n\t"
        "fxch %%st(2)\n\t"
        "fmul %%st(4)\n\t"
        "fxch %%st(1)\n\t"
        "fmul %%st(4)\n\t"
        "fxch %%st(3)\n\t"
        "fmul %%st(4)\n\t"

        "addl $32, %1\n\t"
        "addl $16, %3\n\t"

        "fxch %%st(2)\n\t"
        "fistl %5\n\t"
        "fxch %%st(1)\n\t"
        "fistl 4+%5\n\t"
        "fxch %%st(3)\n\t"
        "fistl 8+%5\n\t"
        "fxch %%st(2)\n\t"
        "fistl 12+%5\n\t"

        "dec %4\n\t"

        "movl %5, %%eax\n\t"
        "movl 4+%5, %%ebx\n\t"
        "fxch %%st(1)\n\t"
        "faddl (%2,%%eax,8)\n\t"
        "fxch %%st(3)\n\t"
        "faddl (%2,%%ebx,8)\n\t"

        "movl 8+%5, %%eax\n\t"
        "movl 12+%5, %%ebx\n\t"
        "fxch %%st(2)\n\t"
        "faddl (%2,%%eax,8)\n\t"
        "fxch %%st(1)\n\t"
        "faddl (%2,%%ebx,8)\n\t"

        "fxch %%st(3)\n\t"
        "fistpl -16(%3)\n\t"
        "fxch %%st(1)\n\t"
        "fistpl -12(%3)\n\t"
        "fistpl -8(%3)\n\t"
        "fistpl -4(%3)\n\t"

        "jnz loop1\n\n"
        : /* no outputs */
        : "t" (istep), "r" (pow_spectrum), "r" (adj_quant_asm), "r" (quant), "r" (1024 / 4), "m" (rx)
        : "%eax", "%ebx", "memory", "cc"
      );
  }
#elif 0
	{
		double x;
		int j, rx;
		for (j = 1024 / 4; j > 0; --j) {
			x = *pow_spectrum++ * istep;
			XRPOW_FTOI(x, rx);
			XRPOW_FTOI(x + QUANTFAC(rx), *quant++);

			x = *pow_spectrum++ * istep;
			XRPOW_FTOI(x, rx);
			XRPOW_FTOI(x + QUANTFAC(rx), *quant++);

			x = *pow_spectrum++ * istep;
			XRPOW_FTOI(x, rx);
			XRPOW_FTOI(x + QUANTFAC(rx), *quant++);

			x = *pow_spectrum++ * istep;
			XRPOW_FTOI(x, rx);
			XRPOW_FTOI(x + QUANTFAC(rx), *quant++);
		}
	}
#else
  {/* from Wilfried.Behne@t-online.de.  Reported to be 2x faster than
      the above code (when not using ASM) on PowerPC */
     	int j;

     	for ( j = 1024/8; j > 0; --j)
     	{
			double	x1, x2, x3, x4, x5, x6, x7, x8;
			int rx1, rx2, rx3, rx4, rx5, rx6, rx7, rx8;
			x1 = *pow_spectrum++ * istep;
			x2 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x1, rx1);
			x3 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x2, rx2);
			x4 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x3, rx3);
			x5 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x4, rx4);
			x6 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x5, rx5);
			x7 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x6, rx6);
			x8 = *pow_spectrum++ * istep;
			XRPOW_FTOI(x7, rx7);
			x1 += QUANTFAC(rx1);
			XRPOW_FTOI(x8, rx8);
			x2 += QUANTFAC(rx2);
			XRPOW_FTOI(x1,*quant++);
			x3 += QUANTFAC(rx3);
			XRPOW_FTOI(x2,*quant++);
			x4 += QUANTFAC(rx4);
			XRPOW_FTOI(x3,*quant++);
			x5 += QUANTFAC(rx5);
			XRPOW_FTOI(x4,*quant++);
			x6 += QUANTFAC(rx6);
			XRPOW_FTOI(x5,*quant++);
			x7 += QUANTFAC(rx7);
			XRPOW_FTOI(x6,*quant++);
			x8 += QUANTFAC(rx8);
			XRPOW_FTOI(x7,*quant++);
			XRPOW_FTOI(x8,*quant++);
     	}
  }
#endif
}

int inner_loop(AACQuantInfo *quantInfo,
			   double *pow_spectrum,
			   int quant[NUM_COEFF],
			   int max_bits)
{
	int bits;

	quantInfo->common_scalefac -= 1;
	do
	{
		quantInfo->common_scalefac += 1;
		quantize(quantInfo, pow_spectrum, quant);
		bits = count_bits(quantInfo, quant);
	} while ( bits > max_bits );

	return bits;
}

int search_common_scalefac(AACQuantInfo *quantInfo,
						   double *pow_spectrum,
						   int quant[NUM_COEFF],
						   int desired_rate)
{
	int flag_GoneOver = 0;
	int CurrentStep = 4;
	int nBits;
	int StepSize = old_startsf;
	int Direction = 0;
	do
	{
		quantInfo->common_scalefac = StepSize;
		quantize(quantInfo, pow_spectrum, quant);
//		nBits = count_bits(quantInfo, quant, quantInfo->book_vector);
		nBits = count_bits(quantInfo, quant);

		if (CurrentStep == 1 ) {
			break; /* nothing to adjust anymore */
		}
		if (flag_GoneOver) {
			CurrentStep /= 2;
		}
		if (nBits > desired_rate) { /* increase Quantize_StepSize */
			if (Direction == -1 && !flag_GoneOver) {
				flag_GoneOver = 1;
				CurrentStep /= 2; /* late adjust */
			}
			Direction = 1;
			StepSize += CurrentStep;
		} else if (nBits < desired_rate) {
			if (Direction == 1 && !flag_GoneOver) {
				flag_GoneOver = 1;
				CurrentStep /= 2; /* late adjust */
			}
			Direction = -1;
			StepSize -= CurrentStep;
		} else break;
    } while (1);

    old_startsf = StepSize;

    return nBits;
}

int calc_noise(AACQuantInfo *quantInfo,
				double *p_spectrum,
				int quant[NUM_COEFF],
				double requant[NUM_COEFF],
				double error_energy[SFB_NUM_MAX],
				double allowed_dist[SFB_NUM_MAX],
				double *over_noise,
				double *tot_noise,
				double *max_noise
				)
{
	int i, sb, sbw;
	int over = 0, count = 0;
	double invQuantFac;
	double linediff, noise;

	*over_noise = 0.0;
	*tot_noise = 0.0;
	*max_noise = -999.0;

	if (quantInfo->block_type!=ONLY_SHORT_WINDOW)
		PulseDecoder(quantInfo, quant);

	for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {

		double max_sb_noise = 0.0;

		sbw = quantInfo->sfb_offset[sb+1] - quantInfo->sfb_offset[sb];

		invQuantFac = pow(2.0, -0.25*(quantInfo->scale_factor[sb] - quantInfo->common_scalefac));

		error_energy[sb] = 0.0;

		for (i = quantInfo->sfb_offset[sb]; i < quantInfo->sfb_offset[sb+1]; i++){
			requant[i] =  pow_quant[min(ABS(quant[i]),8999)] * invQuantFac; 

			/* measure the distortion in each scalefactor band */
			linediff = (double)(ABS(p_spectrum[i]) - ABS(requant[i]));
			linediff *= linediff;
			error_energy[sb] += linediff;
			max_sb_noise = max(max_sb_noise, linediff);
		}
		error_energy[sb] = error_energy[sb] / sbw;		
		
		noise = error_energy[sb] / allowed_dist[sb];

		/* multiplying here is adding in dB */
		*tot_noise *= max(noise, 1E-20);
		if (noise>1) {
			over++;
			/* multiplying here is adding in dB */
			*over_noise *= noise;
		}
		*max_noise = max(*max_noise,noise);
		error_energy[sb] = noise;
		count++;
  	}

	return over;
}

int quant_compare(int best_over, double best_tot_noise, double best_over_noise,
				  double best_max_noise, int over, double tot_noise, double over_noise,
				  double max_noise)
//int quant_compare(double best_tot_noise, double best_over_noise,
//		  double tot_noise, double over_noise)
{
	/*
	noise is given in decibals (db) relative to masking thesholds.

	over_noise:  sum of quantization noise > masking
	tot_noise:   sum of all quantization noise
	max_noise:   max quantization noise

	*/
	int better;

	better =   over  < best_over ||  ( over == best_over
		&& over_noise < best_over_noise )
		||  ( over == best_over && over_noise==best_over_noise
		&& tot_noise < best_tot_noise);

#if 0
	better = ((over < best_over) ||
		((over==best_over) && (over_noise<best_over_noise)) ) ;
	better = min(better, max_noise < best_max_noise);
	better = min(better, tot_noise < best_tot_noise);
	better = min(better, (tot_noise < best_tot_noise) &&
		(max_noise < best_max_noise + 2));
	better = min(better, ( ( (0>=max_noise) && (best_max_noise>2)) ||
		( (0>=max_noise) && (best_max_noise<0) && ((best_max_noise+2)>max_noise) && (tot_noise<best_tot_noise) ) ||
		( (0>=max_noise) && (best_max_noise>0) && ((best_max_noise+2)>max_noise) && (tot_noise<(best_tot_noise+best_over_noise)) ) ||
		( (0<max_noise) && (best_max_noise>-0.5) && ((best_max_noise+1)>max_noise) && ((tot_noise+over_noise)<(best_tot_noise+best_over_noise)) ) ||
		( (0<max_noise) && (best_max_noise>-1) && ((best_max_noise+1.5)>max_noise) && ((tot_noise+over_noise+over_noise)<(best_tot_noise+best_over_noise+best_over_noise)) ) ));
	better = min(better, (over_noise <  best_over_noise)
		|| ((over_noise == best_over_noise)&&(tot_noise < best_tot_noise)));
	better = min(better, (over_noise < best_over_noise)
		||( (over_noise == best_over_noise)
		&&( (max_noise < best_max_noise)
		||( (max_noise == best_max_noise)
		&&(tot_noise <= best_tot_noise)
		)
		)
		));
#endif

	return better;
}


int count_bits(AACQuantInfo* quantInfo,
			   int quant[NUM_COEFF]
//			   ,int output_book_vector[SFB_NUM_MAX*2]
                        )
{
	int i, bits = 0;

	if (quantInfo->block_type==ONLY_SHORT_WINDOW)
		quantInfo->pulseInfo.pulse_data_present = 0;
	else
		PulseCoder(quantInfo, quant);

	/* find a good method to section the scalefactor bands into huffman codebook sections */
	bit_search(quant,              /* Quantized spectral values */
		quantInfo);         /* Quantization information */

    /* Set special codebook for bands coded via PNS  */
    if (quantInfo->block_type != ONLY_SHORT_WINDOW) {     /* long blocks only */
		for(i=0;i<quantInfo->nr_of_sfb;i++) {
			if (quantInfo->pns_sfb_flag[i]) {
				quantInfo->book_vector[i] = PNS_HCB;
			}
		}
    }

	/* calculate the amount of bits needed for encoding the huffman codebook numbers */
	bits += sort_book_numbers(quantInfo,             /* Quantization information */
//		output_book_vector,    /* Output codebook vector, formatted for bitstream */
		NULL,          /* Bitstream */
		0);                    /* Write flag: 0 count, 1 write */

	/* calculate the amount of bits needed for the spectral values */
	quantInfo -> spectralCount = 0;
	for(i=0;i< quantInfo -> nr_of_sfb;i++) {  
		bits += output_bits(
			quantInfo,
			quantInfo->book_vector[i],
			quant,
			quantInfo->sfb_offset[i], 
			quantInfo->sfb_offset[i+1]-quantInfo->sfb_offset[i],
			0);
	}

	/* the number of bits for the scalefactors */
	bits += write_scalefactor_bitstream(
		NULL,             /* Bitstream */  
		0,                        /* Write flag */
		quantInfo
		);

	/* the total amount of bits required */
	return bits;
}

int tf_encode_spectrum_aac(
			   double      *p_spectrum[MAX_TIME_CHANNELS],
			   double      *PsySigMaskRatio[MAX_TIME_CHANNELS],
			   double      allowed_dist[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS],
			   double      energy[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS],
			   enum WINDOW_TYPE block_type[MAX_TIME_CHANNELS],
			   int         sfb_width_table[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS],
//			   int         nr_of_sfb[MAX_TIME_CHANNELS],
			   int         average_block_bits,
//			   int         available_bitreservoir_bits,
//			   int         padding_limit,
			   BsBitStream *fixed_stream,
//			   BsBitStream *var_stream,
//			   int         nr_of_chan,
			   double      *p_reconstructed_spectrum[MAX_TIME_CHANNELS],
//			   int         useShortWindows,
//			   int aacAllowScalefacs,
			   AACQuantInfo* quantInfo,      /* AAC quantization information */ 
			   Ch_Info* ch_info
//			   ,int varBitRate
//			   ,int bitRate
                           )
{
	int quant[NUM_COEFF];
	int s_quant[NUM_COEFF];
	int i;
//	int j=0;
	int k;
	double max_dct_line = 0;
//	int global_gain;
	int store_common_scalefac;
	int best_scale_factor[SFB_NUM_MAX];
	double pow_spectrum[NUM_COEFF];
	double requant[NUM_COEFF];
	int sb;
	int extra_bits;
//	int max_bits;
//	int output_book_vector[SFB_NUM_MAX*2];
	double SigMaskRatio[SFB_NUM_MAX];
	MS_Info *ms_info;
	int *ptr_book_vector;

	/* Set up local pointers to quantInfo elements for convenience */
	int* sfb_offset = quantInfo -> sfb_offset;
	int* scale_factor = quantInfo -> scale_factor;
	int* common_scalefac = &(quantInfo -> common_scalefac);

	int outer_loop_count, notdone;
	int over, better;
	int best_over = 100;
//	int sfb_overflow;
	int best_common_scalefac;
	double noise_thresh;
	double sfQuantFac;
	double over_noise, tot_noise, max_noise;
	double noise[SFB_NUM_MAX];
	double best_max_noise = 0;
	double best_over_noise = 0;
	double best_tot_noise = 0;
//	static int init = -1;

	/* Set block type in quantization info */
	quantInfo -> block_type = block_type[MONO_CHAN];

#if 0
	if (init != quantInfo->block_type) {
		init = quantInfo->block_type;
		compute_ath(quantInfo, ATH);
	}
#endif

	sfQuantFac = pow(2.0, 0.1875);

	/** create the sfb_offset tables **/
	if (quantInfo->block_type == ONLY_SHORT_WINDOW) {

		/* Now compute interleaved sf bands and spectrum */
		sort_for_grouping(
			quantInfo,                       /* ptr to quantization information */
			sfb_width_table[MONO_CHAN],      /* Widths of single window */
			p_spectrum,                      /* Spectral values, noninterleaved */
			SigMaskRatio,
			PsySigMaskRatio[MONO_CHAN]
			);

		extra_bits = 51;
	} else{
		/* For long windows, band are not actually interleaved */
		if ((quantInfo -> block_type == ONLY_LONG_WINDOW) ||  
			(quantInfo -> block_type == LONG_SHORT_WINDOW) ||
			(quantInfo -> block_type == SHORT_LONG_WINDOW)) {
			quantInfo->nr_of_sfb = quantInfo->max_sfb;

			sfb_offset[0] = 0;
			k=0;
			for( i=0; i< quantInfo -> nr_of_sfb; i++ ){
				sfb_offset[i] = k;
				k +=sfb_width_table[MONO_CHAN][i];
				SigMaskRatio[i]=PsySigMaskRatio[MONO_CHAN][i];
			}
			sfb_offset[i] = k;
			extra_bits = 100; /* header bits and more ... */

		} 
	}

	extra_bits += 1;

    /* Take into account bits for TNS data */
    extra_bits += WriteTNSData(quantInfo,fixed_stream,0);    /* Count but don't write */

    if(quantInfo->block_type!=ONLY_SHORT_WINDOW)
		/* Take into account bits for LTP data */
		extra_bits += WriteLTP_PredictorData(quantInfo, fixed_stream, 0); /* Count but don't write */

    /* for short windows, compute interleaved energy here */
    if (quantInfo->block_type==ONLY_SHORT_WINDOW) {
		int numWindowGroups = quantInfo->num_window_groups;
		int maxBand = quantInfo->max_sfb;
		int windowOffset=0;
		int sfb_index=0;
		int g;
		for (g=0;g<numWindowGroups;g++) {
			int numWindowsThisGroup = quantInfo->window_group_length[g];
			int b;
			for (b=0;b<maxBand;b++) {
				double sum=0.0;
				int w;
				for (w=0;w<numWindowsThisGroup;w++) {
					int bandNum = (w+windowOffset)*maxBand + b;
					sum += energy[MONO_CHAN][bandNum];
				}
				energy[MONO_CHAN][sfb_index] = sum/numWindowsThisGroup;
				sfb_index++;
			}
			windowOffset += numWindowsThisGroup;
		}
    } 

	/* initialize the scale_factors that aren't intensity stereo bands */
	for(k=0; k< quantInfo -> nr_of_sfb ;k++) {
		scale_factor[k] = 0;
	}

	/* Mark IS bands by setting book_vector to INTENSITY_HCB */
	ptr_book_vector=quantInfo->book_vector;
	for (k=0;k<quantInfo->nr_of_sfb;k++) {
		ptr_book_vector[k] = 0;
	}

	/* PNS prepare */
	ms_info=&(ch_info->ms_info);
    for(sb=0; sb < quantInfo->nr_of_sfb; sb++ )
		quantInfo->pns_sfb_flag[sb] = 0;

//	if (block_type[MONO_CHAN] != ONLY_SHORT_WINDOW) {     /* long blocks only */
		for(sb = pns_sfb_start; sb < quantInfo->nr_of_sfb; sb++ ) {
			/* Calc. pseudo scalefactor */
			if (energy[0][sb] == 0.0) {
				quantInfo->pns_sfb_flag[sb] = 0;
				continue;
			}

			if ((ms_info->is_present)&&(!ms_info->ms_used[sb])) {
				if ((10*log10(energy[MONO_CHAN][sb]*sfb_width_table[0][sb]+1e-60)<70)||(SigMaskRatio[sb] > 1.0)) {
					quantInfo->pns_sfb_flag[sb] = 1;
					quantInfo->pns_sfb_nrg[sb] = (int) (2.0 * log(energy[0][sb]*sfb_width_table[0][sb]+1e-60) / log(2.0) + 0.5) + PNS_SF_OFFSET;

					/* Erase spectral lines */
					for( i=sfb_offset[sb]; i<sfb_offset[sb+1]; i++ ) {
						p_spectrum[0][i] = 0.0;
					}
				}
			}
		}
//	}

	/* Compute allowed distortion */
	for(sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
		allowed_dist[MONO_CHAN][sb] = energy[MONO_CHAN][sb] * SigMaskRatio[sb];
//		if (allowed_dist[MONO_CHAN][sb] < ATH[sb]) {
//			printf("%d Yes\n", sb);
//			allowed_dist[MONO_CHAN][sb] = ATH[sb];
//		}
//		printf("%d\t\t%.3f\n", sb, SigMaskRatio[sb]);
	}

	/** find the maximum spectral coefficient **/
	/* Bug fix, 3/10/98 CL */
	/* for(i=0; i<NUM_COEFF; i++){ */
	for(i=0; i < sfb_offset[quantInfo->nr_of_sfb]; i++){
		pow_spectrum[i] = (pow(ABS(p_spectrum[0][i]), 0.75));
		sign[i] = sgn(p_spectrum[0][i]);
		if ((ABS(p_spectrum[0][i])) > max_dct_line){
			max_dct_line = ABS(p_spectrum[0][i]);
		}
	}

	if (max_dct_line!=0.0) {
		if ((int)(16/3 * (log(ABS(pow(max_dct_line,0.75)/MAX_QUANT)/log(2.0)))) > old_startsf) {
			old_startsf = (int)(16/3 * (log(ABS(pow(max_dct_line,0.75)/MAX_QUANT)/log(2.0))));
		}
		if ((old_startsf > 200) || (old_startsf < 40))
			old_startsf = 40;
	}

	outer_loop_count = 0;

	notdone = 1;
	if (max_dct_line == 0) {
		notdone = 0;
	}
	while (notdone) { // outer iteration loop

		outer_loop_count++;
		over = 0;
//		sfb_overflow = 0;

//		if (max_dct_line == 0.0)
//			sfb_overflow = 1;

		if (outer_loop_count == 1) {
//			max_bits = search_common_scalefac(quantInfo, p_spectrum[0], pow_spectrum,
//				quant, average_block_bits);
			search_common_scalefac(quantInfo, pow_spectrum, quant, average_block_bits);
		}

//		max_bits = inner_loop(quantInfo, p_spectrum[0], pow_spectrum,
//			quant, average_block_bits) + extra_bits;
		inner_loop(quantInfo, pow_spectrum, quant, average_block_bits);

		store_common_scalefac = quantInfo->common_scalefac;

		if (notdone) {
			over = calc_noise(quantInfo, p_spectrum[0], quant, requant, noise, allowed_dist[0],
				&over_noise, &tot_noise, &max_noise);

			better = quant_compare(best_over, best_tot_noise, best_over_noise,
				best_max_noise, over, tot_noise, over_noise, max_noise);
//			better = quant_compare(best_tot_noise, best_over_noise,
//				               tot_noise, over_noise);

			for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
				if (scale_factor[sb] > 59) {
//					sfb_overflow = 1;
					better = 0;
				}
			}

			if (outer_loop_count == 1)
				better = 1;

			if (better) {
//				best_over = over;
//				best_max_noise = max_noise;
				best_over_noise = over_noise;
				best_tot_noise = tot_noise;
				best_common_scalefac = store_common_scalefac;

				for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
					best_scale_factor[sb] = scale_factor[sb];
				}
				memcpy(s_quant, quant, sizeof(int)*NUM_COEFF);
			}
		}

		if (over == 0) notdone=0;

		if (notdone) {
			notdone = 0;
			noise_thresh = -900;
			for ( sb = 0; sb < quantInfo->nr_of_sfb; sb++ )
				noise_thresh = max(1.05*noise[sb], noise_thresh);
			noise_thresh = min(noise_thresh, 0.0);

			for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
				if ((noise[sb] > noise_thresh)&&(quantInfo->book_vector[sb]!=INTENSITY_HCB)&&(quantInfo->book_vector[sb]!=INTENSITY_HCB2)) {

					allowed_dist[0][sb] *= 2;
					scale_factor[sb]++;
					for (i = quantInfo->sfb_offset[sb]; i < quantInfo->sfb_offset[sb+1]; i++){
						pow_spectrum[i] *= sfQuantFac;
					}
					notdone = 1;
				}
			}
			for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
				if (scale_factor[sb] > 59)
					notdone = 0;
			}
		}

		if (notdone) {
			notdone = 0;
			for (sb = 0; sb < quantInfo->nr_of_sfb; sb++)
				if (scale_factor[sb] == 0)
					notdone = 1;
		}

	}

	if (max_dct_line > 0) {
		*common_scalefac = best_common_scalefac;
		for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
			scale_factor[sb] = best_scale_factor[sb];
//			printf("%d\t%d\n", sb, scale_factor[sb]);
		}
		for (i = 0; i < 1024; i++)
			quant[i] = s_quant[i]*sign[i];
	} else {
		*common_scalefac = 0;
		for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
			scale_factor[sb] = 0;
		}
		for (i = 0; i < 1024; i++)
			quant[i] = 0;
	}

	calc_noise(quantInfo, p_spectrum[0], quant, requant, noise, allowed_dist[0],
			&over_noise, &tot_noise, &max_noise);
//	count_bits(quantInfo, quant, output_book_vector);
	count_bits(quantInfo, quant);
	if (quantInfo->block_type!=ONLY_SHORT_WINDOW)
		PulseDecoder(quantInfo, quant);

//	for( sb=0; sb< quantInfo -> nr_of_sfb; sb++ ) {
//		printf("%d error: %.4f all.dist.: %.4f energy: %.4f\n", sb,
//			noise[sb], allowed_dist[0][sb], energy[0][sb]);
//	}

	/* offset the differenec of common_scalefac and scalefactors by SF_OFFSET  */
	for (i=0; i<quantInfo->nr_of_sfb; i++){
		if ((ptr_book_vector[i]!=INTENSITY_HCB)&&(ptr_book_vector[i]!=INTENSITY_HCB2)) {
			scale_factor[i] = *common_scalefac - scale_factor[i] + SF_OFFSET;
		}
	}
//	*common_scalefac = global_gain = scale_factor[0];
	*common_scalefac = scale_factor[0];

	/* place the codewords and their respective lengths in arrays data[] and len[] respectively */
	/* there are 'counter' elements in each array, and these are variable length arrays depending on the input */

	quantInfo -> spectralCount = 0;
	for(k=0;k< quantInfo -> nr_of_sfb; k++) {
		output_bits(
			quantInfo,
			quantInfo->book_vector[k],
			quant,
			quantInfo->sfb_offset[k],
			quantInfo->sfb_offset[k+1]-quantInfo->sfb_offset[k],
			1);
//		printf("%d\t%d\n",k,quantInfo->book_vector[k]);
	}

	/* write the reconstructed spectrum to the output for use with prediction */
	{
		int i;
		for (sb=0; sb<quantInfo -> nr_of_sfb; sb++){
			if ((ptr_book_vector[sb]==INTENSITY_HCB)||(ptr_book_vector[sb]==INTENSITY_HCB2)){
				for (i=sfb_offset[sb]; i<sfb_offset[sb+1]; i++){
					p_reconstructed_spectrum[0][i]=673;
				}
			} else {
				for (i=sfb_offset[sb]; i<sfb_offset[sb+1]; i++){
					p_reconstructed_spectrum[0][i] = sgn(p_spectrum[0][i]) * requant[i];
				}
			}
		}
	}

	return FNO_ERROR;
}



int sort_for_grouping(AACQuantInfo* quantInfo,        /* ptr to quantization information */
		      int sfb_width_table[],          /* Widths of single window */
		      double *p_spectrum[],           /* Spectral values, noninterleaved */
		      double *SigMaskRatio,
		      double *PsySigMaskRatio)
{
	int i,j,ii;
	int index;
	double tmp[1024];
//	int book=1;
	int group_offset;
	int k=0;
	int windowOffset;

	/* set up local variables for used quantInfo elements */
	int* sfb_offset = quantInfo -> sfb_offset;
	int* nr_of_sfb = &(quantInfo -> nr_of_sfb);
	int* window_group_length;
	int num_window_groups;
	*nr_of_sfb = quantInfo->max_sfb;              /* Init to max_sfb */
	window_group_length = quantInfo -> window_group_length;
	num_window_groups = quantInfo -> num_window_groups;

	/* calc org sfb_offset just for shortblock */
	sfb_offset[k]=0;
	for (k=0; k < 1024; k++) {
		tmp[k] = 0.0;
	}
	for (k=1 ; k <*nr_of_sfb+1; k++) {
		sfb_offset[k] = sfb_offset[k-1] + sfb_width_table[k-1];
	}

	/* sort the input spectral coefficients */
	index = 0;
	group_offset=0;
	for (i=0; i< num_window_groups; i++) {
		for (k=0; k<*nr_of_sfb; k++) {
			for (j=0; j < window_group_length[i]; j++) {
				for (ii=0;ii< sfb_width_table[k];ii++)
					tmp[index++] = p_spectrum[MONO_CHAN][ii+ sfb_offset[k] + 128*j +group_offset];
			}
		}
		group_offset +=  128*window_group_length[i];
	}

	for (k=0; k<1024; k++){
		p_spectrum[MONO_CHAN][k] = tmp[k];
	}

	/* now calc the new sfb_offset table for the whole p_spectrum vector*/
	index = 0;
	sfb_offset[index] = 0;
	index++;
	windowOffset = 0;
	for (i=0; i < num_window_groups; i++) {
		for (k=0 ; k <*nr_of_sfb; k++) {
			/* for this window group and this band, find worst case inverse sig-mask-ratio */
			int bandNum=windowOffset*NSFB_SHORT + k;
			double worstISMR = PsySigMaskRatio[bandNum];
			int w;
			for (w=1;w<window_group_length[i];w++) {
				bandNum=(w+windowOffset)*NSFB_SHORT + k;
				if (PsySigMaskRatio[bandNum]<worstISMR) {
					worstISMR += (PsySigMaskRatio[bandNum] > 0)?PsySigMaskRatio[bandNum]:worstISMR;
				}
			}
			worstISMR /= 2.0;
			SigMaskRatio[k+ i* *nr_of_sfb]=worstISMR/window_group_length[i];
			sfb_offset[index] = sfb_offset[index-1] + sfb_width_table[k]*window_group_length[i] ;
			index++;
		}
		windowOffset += window_group_length[i];
	}

	*nr_of_sfb = *nr_of_sfb * num_window_groups;  /* Number interleaved bands. */

	return 0;
}

#if 0

int
VBR_noise_shaping(lame_global_flags *gfp,
				  double xr[576], III_psy_ratio *ratio,
				  int l3_enc[2][2][576], int *ath_over, int minbits, int maxbits,
				  III_scalefac_t scalefac[2][2],
				  int gr,int ch)
{
	lame_internal_flags *gfc=gfp->internal_flags;
	int       start,end,bw,sfb,l, i, vbrmax;
	III_scalefac_t vbrsf;
	III_scalefac_t save_sf;
	int maxover0,maxover1,maxover0p,maxover1p,maxover,mover;
	int ifqstep;
	III_psy_xmin l3_xmin;
	III_side_info_t * l3_side;
	gr_info *cod_info;  
	double xr34[576];
	int shortblock;
	int global_gain_adjust=0;

	l3_side = &gfc->l3_side;
	cod_info = &l3_side->gr[gr].ch[ch].tt;
	shortblock = (cod_info->block_type == SHORT_TYPE);
	*ath_over = calc_xmin( gfp,xr, ratio, cod_info, &l3_xmin);

	
	for(i=0;i<576;i++) {
		double temp=fabs(xr[i]);
		xr34[i]=sqrt(sqrt(temp)*temp);
	}
	
	
	vbrmax=-10000;
	
	int sflist[3]={-10000,-10000,-10000};
	for ( sfb = 0; sfb < SBPSY_l; sfb++ )   {
		start = gfc->scalefac_band.l[ sfb ];
		end   = gfc->scalefac_band.l[ sfb+1 ];
		bw = end - start;
		vbrsf.l[sfb] = find_scalefac(&xr[start],&xr34[start],1,sfb,
			l3_xmin.l[sfb],bw);
		if (vbrsf.l[sfb]>vbrmax) vbrmax = vbrsf.l[sfb];

		/* code to find the 3 largest (counting duplicates) contributed
		* by Microsoft Employee #12 */
		if (vbrsf.l[sfb]>sflist[0]) {
			sflist[2]=sflist[1];
			sflist[1]=sflist[0];
			sflist[0]=vbrsf.l[sfb];
		} else if (vbrsf.l[sfb]>sflist[1]) {
			sflist[2]=sflist[1];
			sflist[1]=vbrsf.l[sfb];
		} else if (vbrsf.l[sfb]>sflist[2]) {
			sflist[2]=vbrsf.l[sfb];
		}
	}
//	vbrmax=sflist[2];

	/* save a copy of vbrsf, incase we have to recomptue scalefacs */
	memcpy(&save_sf,&vbrsf,sizeof(III_scalefac_t));

	do { 

		memset(&scalefac[gr][ch],0,sizeof(III_scalefac_t));
		
		/******************************************************************
		*
		*  long block scalefacs
		*
		******************************************************************/
		maxover0=0;
		maxover1=0;
		maxover0p=0;
		maxover1p=0;
		
		
		for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {
			maxover0 = Max(maxover0,(vbrmax - vbrsf.l[sfb]) - 2*max_range_long[sfb] );
			maxover0p = Max(maxover0,(vbrmax - vbrsf.l[sfb]) - 2*(max_range_long[sfb]+pretab[sfb]) );
			maxover1 = Max(maxover1,(vbrmax - vbrsf.l[sfb]) - 4*max_range_long[sfb] );
			maxover1p = Max(maxover1,(vbrmax - vbrsf.l[sfb]) - 4*(max_range_long[sfb]+pretab[sfb]));
		}
		mover = Min(maxover0,maxover0p);


		vbrmax -= mover;
		maxover0 -= mover;
		maxover0p -= mover;
		maxover1 -= mover;
		maxover1p -= mover;


		if (maxover0<=0) {
			cod_info->scalefac_scale = 0;
			cod_info->preflag=0;
			vbrmax -= maxover0;
		} else if (maxover0p<=0) {
			cod_info->scalefac_scale = 0;
			cod_info->preflag=1;
			vbrmax -= maxover0p;
		} else if (maxover1==0) {
			cod_info->scalefac_scale = 1;
			cod_info->preflag=0;
		} else if (maxover1p==0) {
			cod_info->scalefac_scale = 1;
			cod_info->preflag=1;
		} else {
			fprintf(stderr,"error vbrquantize.c...\n");
			exit(1);
		}

		
		/* sf =  (cod_info->global_gain-210.0) */
		cod_info->global_gain = vbrmax +210;
		if (cod_info->global_gain>255) cod_info->global_gain=255;
		
		for ( sfb = 0; sfb < SBPSY_l; sfb++ )   
			vbrsf.l[sfb] -= vbrmax;
		
		
		maxover=compute_scalefacs_long(vbrsf.l,cod_info,scalefac[gr][ch].l);
		assert(maxover <=0);
		
		
		/* quantize xr34[] based on computed scalefactors */
		ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
		for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {
			int ifac;
			double fac;
			ifac = ifqstep*scalefac[gr][ch].l[sfb];
			if (cod_info->preflag)
				ifac += ifqstep*pretab[sfb];

			if (ifac+210<Q_MAX) 
				fac = 1/IPOW20(ifac+210);
			else
				fac = pow(2.0,.75*ifac/4.0);

			start = gfc->scalefac_band.l[ sfb ];
			end   = gfc->scalefac_band.l[ sfb+1 ];
			for ( l = start; l < end; l++ ) {
				xr34[l]*=fac;
			}
		}
		
		VBR_quantize_granule(gfp,xr,xr34,l3_enc,ratio,l3_xmin,scalefac,gr,ch);

		if (cod_info->part2_3_length < minbits) {
			/* decrease global gain, recompute scale factors */
			if (*ath_over==0) break;  
			if (cod_info->part2_3_length-cod_info->part2_length== 0) break;
			if (vbrmax+210 ==0 ) break;
		
			--vbrmax;
			--global_gain_adjust;
			memcpy(&vbrsf,&save_sf,sizeof(III_scalefac_t));
			for(i=0;i<576;i++) {
				double temp=fabs(xr[i]);
				xr34[i]=sqrt(sqrt(temp)*temp);
			}

		}

	} while ((cod_info->part2_3_length < minbits));

	while (cod_info->part2_3_length > Min(maxbits,4095)) {
		/* increase global gain, keep exisiting scale factors */
		++cod_info->global_gain;
		VBR_quantize_granule(gfp,xr,xr34,l3_enc,ratio,l3_xmin,scalefac,gr,ch);
		++global_gain_adjust;
	}

	return global_gain_adjust;
}

int tf_encode_spectrum_aac(
			   double      *p_spectrum[MAX_TIME_CHANNELS],
			   double      *PsySigMaskRatio[MAX_TIME_CHANNELS],
			   double      allowed_dist[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS],
			   double      energy[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS],
			   enum WINDOW_TYPE block_type[MAX_TIME_CHANNELS],
			   int         sfb_width_table[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS],
			   int         average_block_bits,
			   BsBitStream *fixed_stream,
			   double      *p_reconstructed_spectrum[MAX_TIME_CHANNELS],
			   AACQuantInfo* quantInfo,      /* AAC quantization information */ 
			   Ch_Info* ch_info
               )
{
	int quant[NUM_COEFF];
	int s_quant[NUM_COEFF];
	int i;
	int k;
	double max_dct_line = 0;
	int store_common_scalefac;
	int best_scale_factor[SFB_NUM_MAX];
	double pow_spectrum[NUM_COEFF];
	double requant[NUM_COEFF];
	int sb;
	int extra_bits;
	double SigMaskRatio[SFB_NUM_MAX];
	MS_Info *ms_info;
	int *ptr_book_vector;

	/* Set up local pointers to quantInfo elements for convenience */
	int* sfb_offset = quantInfo -> sfb_offset;
	int* scale_factor = quantInfo -> scale_factor;
	int* common_scalefac = &(quantInfo -> common_scalefac);

	int outer_loop_count, notdone;
	int over, better;
	int best_over = 100;
	int best_common_scalefac;
	double noise_thresh;
	double sfQuantFac;
	double over_noise, tot_noise, max_noise;
	double noise[SFB_NUM_MAX];
	double best_max_noise = 0;
	double best_over_noise = 0;
	double best_tot_noise = 0;

	/* Set block type in quantization info */
	quantInfo -> block_type = block_type[MONO_CHAN];

	sfQuantFac = pow(2.0, 0.1875);

	/** create the sfb_offset tables **/
	if (quantInfo->block_type == ONLY_SHORT_WINDOW) {

		/* Now compute interleaved sf bands and spectrum */
		sort_for_grouping(
			quantInfo,                       /* ptr to quantization information */
			sfb_width_table[MONO_CHAN],      /* Widths of single window */
			p_spectrum,                      /* Spectral values, noninterleaved */
			SigMaskRatio,
			PsySigMaskRatio[MONO_CHAN]
			);

		extra_bits = 51;
	} else{
		/* For long windows, band are not actually interleaved */
		if ((quantInfo -> block_type == ONLY_LONG_WINDOW) ||  
			(quantInfo -> block_type == LONG_SHORT_WINDOW) ||
			(quantInfo -> block_type == SHORT_LONG_WINDOW)) {
			quantInfo->nr_of_sfb = quantInfo->max_sfb;

			sfb_offset[0] = 0;
			k=0;
			for( i=0; i< quantInfo -> nr_of_sfb; i++ ){
				sfb_offset[i] = k;
				k +=sfb_width_table[MONO_CHAN][i];
				SigMaskRatio[i]=PsySigMaskRatio[MONO_CHAN][i];
			}
			sfb_offset[i] = k;
			extra_bits = 100; /* header bits and more ... */

		} 
	}

	extra_bits += 1;

    /* Take into account bits for TNS data */
    extra_bits += WriteTNSData(quantInfo,fixed_stream,0);    /* Count but don't write */

    if(quantInfo->block_type!=ONLY_SHORT_WINDOW)
		/* Take into account bits for LTP data */
		extra_bits += WriteLTP_PredictorData(quantInfo, fixed_stream, 0); /* Count but don't write */

    /* for short windows, compute interleaved energy here */
    if (quantInfo->block_type==ONLY_SHORT_WINDOW) {
		int numWindowGroups = quantInfo->num_window_groups;
		int maxBand = quantInfo->max_sfb;
		int windowOffset=0;
		int sfb_index=0;
		int g;
		for (g=0;g<numWindowGroups;g++) {
			int numWindowsThisGroup = quantInfo->window_group_length[g];
			int b;
			for (b=0;b<maxBand;b++) {
				double sum=0.0;
				int w;
				for (w=0;w<numWindowsThisGroup;w++) {
					int bandNum = (w+windowOffset)*maxBand + b;
					sum += energy[MONO_CHAN][bandNum];
				}
				energy[MONO_CHAN][sfb_index] = sum/numWindowsThisGroup;
				sfb_index++;
			}
			windowOffset += numWindowsThisGroup;
		}
    } 

	/* initialize the scale_factors that aren't intensity stereo bands */
	for(k=0; k< quantInfo -> nr_of_sfb ;k++) {
		scale_factor[k] = 0;
	}

	/* Mark IS bands by setting book_vector to INTENSITY_HCB */
	ptr_book_vector=quantInfo->book_vector;
	for (k=0;k<quantInfo->nr_of_sfb;k++) {
		ptr_book_vector[k] = 0;
	}

	/* PNS prepare */
	ms_info=&(ch_info->ms_info);
    for(sb=0; sb < quantInfo->nr_of_sfb; sb++ )
		quantInfo->pns_sfb_flag[sb] = 0;

	for(sb = pns_sfb_start; sb < quantInfo->nr_of_sfb; sb++ ) {
		/* Calc. pseudo scalefactor */
		if (energy[0][sb] == 0.0) {
			quantInfo->pns_sfb_flag[sb] = 0;
			continue;
		}

		if ((ms_info->is_present)&&(!ms_info->ms_used[sb])) {
			if ((10*log10(energy[MONO_CHAN][sb]*sfb_width_table[0][sb]+1e-60)<70)||(SigMaskRatio[sb] > 1.0)) {
				quantInfo->pns_sfb_flag[sb] = 1;
				quantInfo->pns_sfb_nrg[sb] = (int) (2.0 * log(energy[0][sb]*sfb_width_table[0][sb]+1e-60) / log(2.0) + 0.5) + PNS_SF_OFFSET;

				/* Erase spectral lines */
				for( i=sfb_offset[sb]; i<sfb_offset[sb+1]; i++ ) {
					p_spectrum[0][i] = 0.0;
				}
			}
		}
	}

	/* Compute allowed distortion */
	for(sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
		allowed_dist[MONO_CHAN][sb] = energy[MONO_CHAN][sb] * SigMaskRatio[sb];
	}

	/** find the maximum spectral coefficient **/
	/* Bug fix, 3/10/98 CL */
	/* for(i=0; i<NUM_COEFF; i++){ */
	for(i=0; i < sfb_offset[quantInfo->nr_of_sfb]; i++){
		pow_spectrum[i] = (pow(ABS(p_spectrum[0][i]), 0.75));
		sign[i] = sgn(p_spectrum[0][i]);
		if ((ABS(p_spectrum[0][i])) > max_dct_line){
			max_dct_line = ABS(p_spectrum[0][i]);
		}
	}

	if (max_dct_line!=0.0) {
		if ((int)(16/3 * (log(ABS(pow(max_dct_line,0.75)/MAX_QUANT)/log(2.0)))) > old_startsf) {
			old_startsf = (int)(16/3 * (log(ABS(pow(max_dct_line,0.75)/MAX_QUANT)/log(2.0))));
		}
		if ((old_startsf > 200) || (old_startsf < 40))
			old_startsf = 40;
	}

	outer_loop_count = 0;
	max_bits = average_block_bits + 1;

	do {

		int adjusted,shortblock;
		totbits=0;
				
		/* ENCODE this data first pass, and on future passes unless it uses
		* a very small percentage of the max_frame_bits  */
		if (max_bits > average_block_bits) {

			shortblock = (quantInfo->block_type == ONLY_SHORT_WINDOW);

			/* Adjust allowed masking based on quality setting */
			masking_lower_db = dbQ[0] + qadjust;

//			if (pe[gr][ch]>750)
//				masking_lower_db -= 4*(pe[gr][ch]-750.)/750.;

			masking_lower = pow(10.0,masking_lower_db/10);
			adjusted = VBR_noise_shaping (quantInfo, p_spectrum[0], p_ratio[0], quant,
				average_block_bits, &max_bits);
		}
		bits_ok=1;
		if (max_bits > average_block_bits) {
			qadjust += Max(.25,(max_bits - average_block_bits)/300.0);
			bits_ok=0;
		}
		
	} while (!bits_ok);

	if (max_dct_line > 0) {
		*common_scalefac = best_common_scalefac;
		for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
			scale_factor[sb] = best_scale_factor[sb];
		}
		for (i = 0; i < 1024; i++)
			quant[i] = s_quant[i]*sign[i];
	} else {
		*common_scalefac = 0;
		for (sb = 0; sb < quantInfo->nr_of_sfb; sb++) {
			scale_factor[sb] = 0;
		}
		for (i = 0; i < 1024; i++)
			quant[i] = 0;
	}

	calc_noise(quantInfo, p_spectrum[0], quant, requant, noise, allowed_dist[0],
			&over_noise, &tot_noise, &max_noise);
	count_bits(quantInfo, quant);
	if (quantInfo->block_type!=ONLY_SHORT_WINDOW)
		PulseDecoder(quantInfo, quant);

	/* offset the differenec of common_scalefac and scalefactors by SF_OFFSET  */
	for (i=0; i<quantInfo->nr_of_sfb; i++){
		if ((ptr_book_vector[i]!=INTENSITY_HCB)&&(ptr_book_vector[i]!=INTENSITY_HCB2)) {
			scale_factor[i] = *common_scalefac - scale_factor[i] + SF_OFFSET;
		}
	}
	*common_scalefac = scale_factor[0];

	/* place the codewords and their respective lengths in arrays data[] and len[] respectively */
	/* there are 'counter' elements in each array, and these are variable length arrays depending on the input */

	quantInfo -> spectralCount = 0;
	for(k=0;k< quantInfo -> nr_of_sfb; k++) {
		output_bits(
			quantInfo,
			quantInfo->book_vector[k],
			quant,
			quantInfo->sfb_offset[k],
			quantInfo->sfb_offset[k+1]-quantInfo->sfb_offset[k],
			1);
	}

	/* write the reconstructed spectrum to the output for use with prediction */
	{
		int i;
		for (sb=0; sb<quantInfo -> nr_of_sfb; sb++){
			if ((ptr_book_vector[sb]==INTENSITY_HCB)||(ptr_book_vector[sb]==INTENSITY_HCB2)){
				for (i=sfb_offset[sb]; i<sfb_offset[sb+1]; i++){
					p_reconstructed_spectrum[0][i]=673;
				}
			} else {
				for (i=sfb_offset[sb]; i<sfb_offset[sb+1]; i++){
					p_reconstructed_spectrum[0][i] = sgn(p_spectrum[0][i]) * requant[i];
				}
			}
		}
	}

	return FNO_ERROR;
}
#endif
#if 0



/*
 *	MP3 quantization
 *
 *	Copyright (c) 1999 Mark Taylor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#if (defined(__GNUC__) && defined(__i386__))
#define USE_GNUC_ASM
#endif
#ifdef _MSC_VER
#define USE_MSC_ASM
#endif



/*********************************************************************
 * XRPOW_FTOI is a macro to convert floats to ints.  
 * if XRPOW_FTOI(x) = nearest_int(x), then QUANTFAC(x)=adj43asm[x]
 *                                         ROUNDFAC= -0.0946
 *
 * if XRPOW_FTOI(x) = floor(x), then QUANTFAC(x)=asj43[x]   
 *                                   ROUNDFAC=0.4054
 *********************************************************************/
#ifdef USE_GNUC_ASM
#  define QUANTFAC(rx)  adj43asm[rx]
#  define ROUNDFAC -0.0946
#  define XRPOW_FTOI(src, dest) \
     asm ("fistpl %0 " : "=m"(dest) : "t"(src) : "st")
#elif defined (USE_MSC_ASM)
#  define QUANTFAC(rx)  adj43asm[rx]
#  define ROUNDFAC -0.0946
#  define XRPOW_FTOI(src, dest) do { \
     double src_ = (src); \
     int dest_; \
     { \
       __asm fld src_ \
       __asm fistp dest_ \
     } \
     (dest) = dest_; \
   } while (0)
#else
#  define QUANTFAC(rx)  adj43[rx]
#  define ROUNDFAC 0.4054
#  define XRPOW_FTOI(src,dest) ((dest) = (int)(src))
#endif






double calc_sfb_noise(double *xr, double *xr34, int stride, int bw, int sf)
{
  int j;
  double xfsf=0;
  double sfpow,sfpow34;

  sfpow = pow(2.0, sf+210); /*pow(2.0,sf/4.0); */
  sfpow34  = IPOW20(sf+210); /*pow(sfpow,-3.0/4.0);*/

  for ( j=0; j < stride*bw ; j += stride) {
    int ix;
    double temp;

    if (xr34[j]*sfpow34 > IXMAX_VAL) return -1;

    temp = xr34[j]*sfpow34;
    XRPOW_FTOI(temp, ix);
    XRPOW_FTOI(temp + QUANTFAC(ix), ix);
    temp = fabs(xr[j])- pow43[ix]*sfpow;
    temp *= temp;
    
    xfsf += temp;
  }
  return xfsf/bw;
}





double calc_sfb_noise_ave(double *xr, double *xr34, int stride, int bw,int sf)
{
  int j;
  double xfsf=0, xfsf_p1=0, xfsf_m1=0;
  double sfpow34,sfpow34_p1,sfpow34_m1;
  double sfpow,sfpow_p1,sfpow_m1;

  sfpow = POW20(sf+210); /*pow(2.0,sf/4.0); */
  sfpow34  = IPOW20(sf+210); /*pow(sfpow,-3.0/4.0);*/

  sfpow_m1 = sfpow*.8408964153;  /* pow(2,(sf-1)/4.0) */
  sfpow34_m1 = sfpow34*1.13878863476;       /* .84089 ^ -3/4 */

  sfpow_p1 = sfpow*1.189207115;  
  sfpow34_p1 = sfpow34*0.878126080187;

  for ( j=0; j < stride*bw ; j += stride) {
    int ix;
    double temp,temp_p1,temp_m1;

    if (xr34[j]*sfpow34_m1 > IXMAX_VAL) return -1;

    temp = xr34[j]*sfpow34;
    XRPOW_FTOI(temp, ix);
    XRPOW_FTOI(temp + QUANTFAC(ix), ix);
    temp = fabs(xr[j])- pow43[ix]*sfpow;
    temp *= temp;

    temp_p1 = xr34[j]*sfpow34_p1;
    XRPOW_FTOI(temp_p1, ix);
    XRPOW_FTOI(temp_p1 + QUANTFAC(ix), ix);
    temp_p1 = fabs(xr[j])- pow43[ix]*sfpow_p1;
    temp_p1 *= temp_p1;
    
    temp_m1 = xr34[j]*sfpow34_m1;
    XRPOW_FTOI(temp_m1, ix);
    XRPOW_FTOI(temp_m1 + QUANTFAC(ix), ix);
    temp_m1 = fabs(xr[j])- pow43[ix]*sfpow_m1;
    temp_m1 *= temp_m1;

    xfsf += temp;
    xfsf_p1 += temp_p1;
    xfsf_m1 += temp_m1;
  }
  if (xfsf_p1>xfsf) xfsf = xfsf_p1;
  if (xfsf_m1>xfsf) xfsf = xfsf_m1;
  return xfsf/bw;
}



int find_scalefac(double *xr,double *xr34,int stride,int sfb,
		     double l3_xmin,int bw)
{
  double xfsf;
  int i,sf,sf_ok,delsf;

  /* search will range from sf:  -209 -> 45  */
  sf = -82;
  delsf = 128;

  sf_ok=10000;
  for (i=0; i<7; i++) {
    delsf /= 2;
    //      xfsf = calc_sfb_noise(xr,xr34,stride,bw,sf);
    xfsf = calc_sfb_noise_ave(xr,xr34,stride,bw,sf);

    if (xfsf < 0) {
      /* scalefactors too small */
      sf += delsf;
    }else{
      if (sf_ok==10000) sf_ok=sf;  
      if (xfsf > l3_xmin)  {
	/* distortion.  try a smaller scalefactor */
	sf -= delsf;
      }else{
	sf_ok = sf;
	sf += delsf;
      }
    }
  } 
  assert(sf_ok!=10000);
  //  assert(delsf==1);  /* when for loop goes up to 7 */

  return sf;
}



/*
    sfb=0..5  scalefac < 16 
    sfb>5     scalefac < 8

    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    ol_sf =  (cod_info->global_gain-210.0);
    ol_sf -= 8*cod_info->subblock_gain[i];
    ol_sf -= ifqstep*scalefac[gr][ch].s[sfb][i];

*/
int compute_scalefacs_short(int sf[SBPSY_s][3],gr_info *cod_info,
int scalefac[SBPSY_s][3],unsigned int sbg[3])
{
  int maxrange,maxrange1,maxrange2,maxover;
  int sfb,i;
  int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;

  maxover=0;
  maxrange1 = 15;
  maxrange2 = 7;


  for (i=0; i<3; ++i) {
    int maxsf1=0,maxsf2=0,minsf=1000;
    /* see if we should use subblock gain */
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      if (sfb < 6) {
	if (-sf[sfb][i]>maxsf1) maxsf1 = -sf[sfb][i];
      } else {
	if (-sf[sfb][i]>maxsf2) maxsf2 = -sf[sfb][i];
      }
      if (-sf[sfb][i]<minsf) minsf = -sf[sfb][i];
    }

    /* boost subblock gain as little as possible so we can
     * reach maxsf1 with scalefactors 
     * 8*sbg >= maxsf1   
     */
    maxsf1 = Max(maxsf1-maxrange1*ifqstep,maxsf2-maxrange2*ifqstep);
    sbg[i]=0;
    if (minsf >0 ) sbg[i] = floor(.125*minsf + .001);
    if (maxsf1 > 0)  sbg[i]  = Max(sbg[i],maxsf1/8 + (maxsf1 % 8 != 0));
    if (sbg[i] > 7) sbg[i]=7;


    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      sf[sfb][i] += 8*sbg[i];

      if (sf[sfb][i] < 0) {
	maxrange = sfb < 6 ? maxrange1 : maxrange2;
	scalefac[sfb][i]=-sf[sfb][i]/ifqstep + (-sf[sfb][i]%ifqstep != 0);
	if (scalefac[sfb][i]>maxrange) scalefac[sfb][i]=maxrange;
	
	if (-(sf[sfb][i] + scalefac[sfb][i]*ifqstep) >maxover)  {
	  maxover=-(sf[sfb][i] + scalefac[sfb][i]*ifqstep);
	}
      }
    }
  }

  return maxover;
}





int max_range_short[SBPSY_s]=
{15, 15, 15, 15, 15, 15 ,  7,    7,    7,    7,   7,     7 };
int max_range_long[SBPSY_l]=
{15,   15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   7,   7,   7,   7,   7,   7,   7,    7,    7,    7};


/*
	  ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
	  ol_sf =  (cod_info->global_gain-210.0);
	  ol_sf -= ifqstep*scalefac[gr][ch].l[sfb];
	  if (cod_info->preflag && sfb>=11) 
	  ol_sf -= ifqstep*pretab[sfb];
*/
int compute_scalefacs_long(int sf[SBPSY_l],gr_info *cod_info,int scalefac[SBPSY_l])
{
  int sfb;
  int maxover;
  int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
  

  if (cod_info->preflag)
    for ( sfb = 11; sfb < SBPSY_l; sfb++ ) 
      sf[sfb] += pretab[sfb]*ifqstep;


  maxover=0;
  for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {

    if (sf[sfb]<0) {
      /* ifqstep*scalefac >= -sf[sfb], so round UP */
      scalefac[sfb]=-sf[sfb]/ifqstep  + (-sf[sfb] % ifqstep != 0);
      if (scalefac[sfb] > max_range_long[sfb]) scalefac[sfb]=max_range_long[sfb];
      
      /* sf[sfb] should now be positive: */
      if (  -(sf[sfb] + scalefac[sfb]*ifqstep)  > maxover) {
	maxover = -(sf[sfb] + scalefac[sfb]*ifqstep);
      }
    }
  }

  return maxover;
}
  
  






/************************************************************************
 *
 * quantize and encode with the given scalefacs and global gain
 *
 * compute scalefactors, l3_enc, and return number of bits needed to encode
 *
 *
 ************************************************************************/
void
VBR_quantize_granule(lame_global_flags *gfp,
		     double xr[576],
                double xr34[576], int l3_enc[2][2][576],
		     III_psy_ratio *ratio,      III_psy_xmin l3_xmin,
                III_scalefac_t scalefac[2][2],int gr, int ch)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  gr_info *cod_info;  
  III_side_info_t * l3_side;
  l3_side = &gfc->l3_side;
  cod_info = &l3_side->gr[gr].ch[ch].tt;

  /* encode scalefacs */
  if ( gfp->version == 1 ) 
    scale_bitcount(&scalefac[gr][ch], cod_info);
  else
    scale_bitcount_lsf(&scalefac[gr][ch], cod_info);


  /* quantize xr34 */
  cod_info->part2_3_length = count_bits(gfp,l3_enc[gr][ch],xr34,cod_info);
  cod_info->part2_3_length += cod_info->part2_length;
  
  if (gfc->use_best_huffman==1 && cod_info->block_type != SHORT_TYPE) {
    best_huffman_divide(gfc, gr, ch, cod_info, l3_enc[gr][ch]);
  }

  return;
}
  

















/************************************************************************
 *
 * VBR_noise_shaping()
 *
 * compute scalefactors, l3_enc, and return number of bits needed to encode
 *
 * return code:    0   scalefactors were found with all noise < masking
 *
 *               n>0   scalefactors required too many bits.  global gain
 *                     was decreased by n
 *                     If n is large, we should probably recompute scalefacs
 *                     with a lower quality.
 *
 *               n<0   scalefactors used less than minbits.
 *                     global gain was increased by n.  
 *                     If n is large, might want to recompute scalefacs
 *                     with a higher quality setting?
 *
 ************************************************************************/
int
VBR_noise_shaping
(
 lame_global_flags *gfp,
 double xr[576], III_psy_ratio *ratio,
 int l3_enc[2][2][576], int *ath_over, int minbits, int maxbits,
 III_scalefac_t scalefac[2][2],
 int gr,int ch)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  int       start,end,bw,sfb,l, i, vbrmax;
  III_scalefac_t vbrsf;
  III_scalefac_t save_sf;
  int maxover0,maxover1,maxover0p,maxover1p,maxover,mover;
  int ifqstep;
  III_psy_xmin l3_xmin;
  III_side_info_t * l3_side;
  gr_info *cod_info;  
  double xr34[576];
  int shortblock;
  int global_gain_adjust=0;

  l3_side = &gfc->l3_side;
  cod_info = &l3_side->gr[gr].ch[ch].tt;
  shortblock = (cod_info->block_type == SHORT_TYPE);
  *ath_over = calc_xmin( gfp,xr, ratio, cod_info, &l3_xmin);

  
  for(i=0;i<576;i++) {
    double temp=fabs(xr[i]);
    xr34[i]=sqrt(sqrt(temp)*temp);
  }
  
  
  vbrmax=-10000;
  if (shortblock) {
    for ( sfb = 0; sfb < SBPSY_s; sfb++ )  {
      for ( i = 0; i < 3; i++ ) {
	start = gfc->scalefac_band.s[ sfb ];
	end   = gfc->scalefac_band.s[ sfb+1 ];
	bw = end - start;
	vbrsf.s[sfb][i] = find_scalefac(&xr[3*start+i],&xr34[3*start+i],3,sfb,
					l3_xmin.s[sfb][i],bw);
	if (vbrsf.s[sfb][i]>vbrmax) vbrmax=vbrsf.s[sfb][i];
      }
    }
  }else{
    int sflist[3]={-10000,-10000,-10000};
    for ( sfb = 0; sfb < SBPSY_l; sfb++ )   {
      start = gfc->scalefac_band.l[ sfb ];
      end   = gfc->scalefac_band.l[ sfb+1 ];
      bw = end - start;
      vbrsf.l[sfb] = find_scalefac(&xr[start],&xr34[start],1,sfb,
				   l3_xmin.l[sfb],bw);
      if (vbrsf.l[sfb]>vbrmax) vbrmax = vbrsf.l[sfb];

      /* code to find the 3 largest (counting duplicates) contributed
       * by Microsoft Employee #12 */
      if (vbrsf.l[sfb]>sflist[0]) {
	sflist[2]=sflist[1];
	sflist[1]=sflist[0];
	sflist[0]=vbrsf.l[sfb];
      } else if (vbrsf.l[sfb]>sflist[1]) {
	sflist[2]=sflist[1];
	sflist[1]=vbrsf.l[sfb];
      } else if (vbrsf.l[sfb]>sflist[2]) {
	sflist[2]=vbrsf.l[sfb];
      }
    }
    //    vbrmax=sflist[2];
  } /* compute needed scalefactors */

  /* save a copy of vbrsf, incase we have to recomptue scalefacs */
  memcpy(&save_sf,&vbrsf,sizeof(III_scalefac_t));

#undef SCALEFAC_SCALE

  do { 

  memset(&scalefac[gr][ch],0,sizeof(III_scalefac_t));
    
  if (shortblock) {
    /******************************************************************
     *
     *  short block scalefacs
     *
     ******************************************************************/
    maxover0=0;
    maxover1=0;
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      for ( i = 0; i < 3; i++ ) {
	maxover0 = Max(maxover0,(vbrmax - vbrsf.s[sfb][i]) - (4*14 + 2*max_range_short[sfb]) );
	maxover1 = Max(maxover1,(vbrmax - vbrsf.s[sfb][i]) - (4*14 + 4*max_range_short[sfb]) );
      }
    }
#ifdef SCALEFAC_SCALE
    mover = Min(maxover0,maxover1);
#else
    mover = maxover0; 
#endif

    vbrmax -= mover;
    maxover0 -= mover;
    maxover1 -= mover;

    if (maxover0==0) 
      cod_info->scalefac_scale = 0;
    else if (maxover1==0)
      cod_info->scalefac_scale = 1;


    /* sf =  (cod_info->global_gain-210.0) */
    cod_info->global_gain = vbrmax +210;
    if (cod_info->global_gain>255) cod_info->global_gain=255;
    
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      for ( i = 0; i < 3; i++ ) {
	vbrsf.s[sfb][i]-=vbrmax;
      }
    }
    maxover=compute_scalefacs_short(vbrsf.s,cod_info,scalefac[gr][ch].s,cod_info->subblock_gain);
    assert(maxover <=0);
    {
      /* adjust global_gain so at least 1 subblock gain = 0 */
      int minsfb=999;
      for (i=0; i<3; i++) minsfb = Min(minsfb,cod_info->subblock_gain[i]);
      minsfb = Min(cod_info->global_gain/8,minsfb);
      vbrmax -= 8*minsfb; 
      cod_info->global_gain -= 8*minsfb;
      for (i=0; i<3; i++) cod_info->subblock_gain[i] -= minsfb;
    }
    
    
    
    /* quantize xr34[] based on computed scalefactors */
    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      start = gfc->scalefac_band.s[ sfb ];
      end   = gfc->scalefac_band.s[ sfb+1 ];
      for (i=0; i<3; i++) {
	int ifac;
	double fac;
	ifac = (8*cod_info->subblock_gain[i]+ifqstep*scalefac[gr][ch].s[sfb][i]);
	if (ifac+210<Q_MAX) 
	  fac = 1/IPOW20(ifac+210);
	else
	  fac = pow(2.0,.75*ifac/4.0);
	for ( l = start; l < end; l++ ) 
	  xr34[3*l +i]*=fac;
      }
    }
    
    
    
  }else{
    /******************************************************************
     *
     *  long block scalefacs
     *
     ******************************************************************/
    maxover0=0;
    maxover1=0;
    maxover0p=0;
    maxover1p=0;
    
    
    for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {
      maxover0 = Max(maxover0,(vbrmax - vbrsf.l[sfb]) - 2*max_range_long[sfb] );
      maxover0p = Max(maxover0,(vbrmax - vbrsf.l[sfb]) - 2*(max_range_long[sfb]+pretab[sfb]) );
      maxover1 = Max(maxover1,(vbrmax - vbrsf.l[sfb]) - 4*max_range_long[sfb] );
      maxover1p = Max(maxover1,(vbrmax - vbrsf.l[sfb]) - 4*(max_range_long[sfb]+pretab[sfb]));
    }
    mover = Min(maxover0,maxover0p);
#ifdef SCALEFAC_SCALE
    mover = Min(mover,maxover1);
    mover = Min(mover,maxover1p);
#endif


    vbrmax -= mover;
    maxover0 -= mover;
    maxover0p -= mover;
    maxover1 -= mover;
    maxover1p -= mover;


    if (maxover0<=0) {
      cod_info->scalefac_scale = 0;
      cod_info->preflag=0;
      vbrmax -= maxover0;
    } else if (maxover0p<=0) {
      cod_info->scalefac_scale = 0;
      cod_info->preflag=1;
      vbrmax -= maxover0p;
    } else if (maxover1==0) {
      cod_info->scalefac_scale = 1;
      cod_info->preflag=0;
    } else if (maxover1p==0) {
      cod_info->scalefac_scale = 1;
      cod_info->preflag=1;
    } else {
      fprintf(stderr,"error vbrquantize.c...\n");
      exit(1);
    }

    
    /* sf =  (cod_info->global_gain-210.0) */
    cod_info->global_gain = vbrmax +210;
    if (cod_info->global_gain>255) cod_info->global_gain=255;
    
    for ( sfb = 0; sfb < SBPSY_l; sfb++ )   
      vbrsf.l[sfb] -= vbrmax;
    
    
    maxover=compute_scalefacs_long(vbrsf.l,cod_info,scalefac[gr][ch].l);
    assert(maxover <=0);
    
    
    /* quantize xr34[] based on computed scalefactors */
    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {
      int ifac;
      double fac;
      ifac = ifqstep*scalefac[gr][ch].l[sfb];
      if (cod_info->preflag)
	ifac += ifqstep*pretab[sfb];

      if (ifac+210<Q_MAX) 
	fac = 1/IPOW20(ifac+210);
      else
	fac = pow(2.0,.75*ifac/4.0);

      start = gfc->scalefac_band.l[ sfb ];
      end   = gfc->scalefac_band.l[ sfb+1 ];
      for ( l = start; l < end; l++ ) {
    	xr34[l]*=fac;
      }
    }
  } 
  
  VBR_quantize_granule(gfp,xr,xr34,l3_enc,ratio,l3_xmin,scalefac,gr,ch);

  if (cod_info->part2_3_length < minbits) {
    /* decrease global gain, recompute scale factors */
    if (*ath_over==0) break;  
    if (cod_info->part2_3_length-cod_info->part2_length== 0) break;
    if (vbrmax+210 ==0 ) break;
    
    //        printf("not enough bits, decreasing vbrmax. g_gainv=%i\n",cod_info->global_gain);
    //        printf("%i minbits=%i   part2_3_length=%i  part2=%i\n",
    //    	   gfp->frameNum,minbits,cod_info->part2_3_length,cod_info->part2_length);
    --vbrmax;
    --global_gain_adjust;
    memcpy(&vbrsf,&save_sf,sizeof(III_scalefac_t));
    for(i=0;i<576;i++) {
      double temp=fabs(xr[i]);
      xr34[i]=sqrt(sqrt(temp)*temp);
    }

  }

  } while ((cod_info->part2_3_length < minbits));

  while (cod_info->part2_3_length > Min(maxbits,4095)) {
    /* increase global gain, keep exisiting scale factors */
    ++cod_info->global_gain;
    VBR_quantize_granule(gfp,xr,xr34,l3_enc,ratio,l3_xmin,scalefac,gr,ch);
    ++global_gain_adjust;
  }

  return global_gain_adjust;
}




void
VBR_quantize(lame_global_flags *gfp,
                double pe[2][2], double ms_ener_ratio[2],
                double xr[2][2][576], III_psy_ratio ratio[2][2],
                int l3_enc[2][2][576],
                III_scalefac_t scalefac[2][2])
{
	lame_internal_flags *gfc=gfp->internal_flags;
	int minbits,maxbits,max_frame_bits,totbits,gr,ch,i,bits_ok;
	int bitsPerFrame,mean_bits;
	double qadjust;
	III_side_info_t * l3_side;
	gr_info *cod_info;  
	int ath_over[2][2];
	double masking_lower_db;
	// static const double dbQ[10]={-6.0,-5.0,-4.0,-3.0, -2.0, -1.0, -.25, .5, 1.25, 2.0};
	/* from quantize.c VBR algorithm */
	static const double dbQ[10]={-5.0,-3.75,-2.5,-1.25,  0,  0.4,  0.8, 1.2, 1.6,2.0};

	qadjust=-1;   /* start with -1 db quality improvement over quantize.c VBR */


	ATH_lower = (4 - VBR_q) * 4.0; 
	if (ATH_lower < 0) ATH_lower=0;
	iteration_init(gfp,l3_side,l3_enc);

	gfc->bitrate_index=gfc->VBR_min_bitrate;
	getframebits(gfp,&bitsPerFrame, &mean_bits);
	minbits = .4*(mean_bits/numChannel);

	gfc->bitrate_index=gfc->VBR_max_bitrate;
	getframebits(gfp,&bitsPerFrame, &mean_bits);
	max_frame_bits = ResvFrameBegin(gfp,l3_side, mean_bits, bitsPerFrame);
	maxbits=2.5*(mean_bits/numChannel);

	{
	/* compute a target  mean_bits based on compression ratio 
	* which was set based on VBR_q  
		*/
		int bit_rate = gfp->out_samplerate*16*numChannel/(1000.0*gfp->compression_ratio);
		bitsPerFrame = (bit_rate*gfp->framesize*1000)/gfp->out_samplerate;
		mean_bits = (bitsPerFrame - 8*gfc->sideinfo_len) / gfc->mode_gr;
	}

	minbits=Max(minbits,.4*(mean_bits/numChannel));
	maxbits=Min(maxbits,2.5*(mean_bits/numChannel));

	for (gr = 0; gr < gfc->mode_gr; gr++) {
		for (ch = 0; ch < gfc->stereo; ch++) { 
			cod_info->part2_3_length=LARGE_BITS;
		}
	}



	/* 
	* loop over all ch,gr, encoding anything with bits > .5*(max_frame_bits/4)
	*
	* If a particular granule uses way too many bits, it will be re-encoded
	* on the next iteration of the loop (with a lower quality setting).  
	* But granules which dont use
	* use too many bits will not be re-encoded.
	*
	* minbits:  minimum allowed bits for 1 granule 1 channel
	* maxbits:  maximum allowwed bits for 1 granule 1 channel
	* max_frame_bits:  maximum allowed bits for entire frame
	* (max_frame_bits/4)   estimate of average bits per granule per channel
	* 
	*/

	do {

		totbits=0;
		for (gr = 0; gr < gfc->mode_gr; gr++) {
			for (ch = 0; ch < numChannel; ch++) { 
				int adjusted,shortblock;
				
				/* ENCODE this data first pass, and on future passes unless it uses
				* a very small percentage of the max_frame_bits  */
				if (cod_info->part2_3_length > (max_frame_bits/(2*gfc->stereo))) {

					shortblock = (quantInfo->block_type == ONLY_SHORT_WINDOW);

					/* Adjust allowed masking based on quality setting */
					masking_lower_db = dbQ[gfp->VBR_q] + qadjust;

//					if (pe[gr][ch]>750)
//						masking_lower_db -= 4*(pe[gr][ch]-750.)/750.;
					
					gfc->masking_lower = pow(10.0,masking_lower_db/10);
					adjusted = VBR_noise_shaping (gfp,xr[gr][ch],&ratio[gr][ch],l3_enc,&ath_over[gr][ch],minbits,maxbits,scalefac,gr,ch);
				}
				totbits += cod_info->part2_3_length;
			}
		}
		bits_ok=1;
		if (totbits>max_frame_bits) {
			qadjust += Max(.25,(totbits-max_frame_bits)/300.0);
			bits_ok=0;
		}
		
	} while (!bits_ok);


	/* find optimal scalefac storage.  Cant be done above because
	* might enable scfsi which breaks the interation loops */
	totbits=0;
	for (gr = 0; gr < gfc->mode_gr; gr++) {
		for (ch = 0; ch < gfc->stereo; ch++) {
			best_scalefac_store(gfp,gr, ch, l3_enc, l3_side, scalefac);
			totbits += l3_side->gr[gr].ch[ch].tt.part2_3_length;
		}
	}

	
	for( gfc->bitrate_index = (gfp->VBR_hard_min ? gfc->VBR_min_bitrate : 1);
	gfc->bitrate_index < gfc->VBR_max_bitrate;
	gfc->bitrate_index++    ) {

		getframebits (gfp,&bitsPerFrame, &mean_bits);
		maxbits = ResvFrameBegin(gfp,l3_side, mean_bits, bitsPerFrame);
		if (totbits <= maxbits) break;
	}
	if (gfc->bitrate_index == gfc->VBR_max_bitrate) {
		getframebits (gfp,&bitsPerFrame, &mean_bits);
		maxbits = ResvFrameBegin(gfp,l3_side, mean_bits, bitsPerFrame);
	}

	//  printf("%i total_bits=%i max_frame_bits=%i index=%i  \n",gfp->frameNum,totbits,max_frame_bits,gfc->bitrate_index);

	for (gr = 0; gr < gfc->mode_gr; gr++) {
		for (ch = 0; ch < gfc->stereo; ch++) {
			cod_info = &l3_side->gr[gr].ch[ch].tt;


			ResvAdjust (gfp,cod_info, l3_side, mean_bits);
			
			/*******************************************************************
			* set the sign of l3_enc from the sign of xr
			*******************************************************************/
			for ( i = 0; i < 576; i++) {
				if (xr[gr][ch][i] < 0) l3_enc[gr][ch][i] *= -1;
			}
		}
	}
	ResvFrameEnd (gfp,l3_side, mean_bits);
}

#endif
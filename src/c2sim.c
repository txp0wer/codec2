/*---------------------------------------------------------------------------*\

  FILE........: c2sim.c
  AUTHOR......: David Rowe
  DATE CREATED: 20/8/2010

  Codec2 simulation.  Combines encoder and decoder and allows
  switching in and out various algorithms and quantisation steps. Used
  for algorithm development.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2009 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2.1, as
  published by the Free Software Foundation.  This program is
  distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>

#include "defines.h"
#include "sine.h"
#include "nlp.h"
#include "dump.h"
#include "lpc.h"
#include "lsp.h"
#include "quantise.h"
#include "phase.h"
#include "postfilter.h"
#include "interp.h"
#include "ampexp.h"
#include "phaseexp.h"
#include "bpf.h"
#include "bpfb.h"

void synth_one_frame(codec2_fftr_cfg fftr_inv_cfg, short buf[], MODEL *model, float Sn_[], float Pn[], int prede, float *de_mem, float gain);
void print_help(const struct option *long_options, int num_opts, char* argv[]);
static void ear_protection(float in_out[], int n);


/*---------------------------------------------------------------------------*\

				MAIN

\*---------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    FILE *fout = NULL;	/* output speech file                    */
    FILE *fin;		/* input speech file                     */
    short buf[N_SAMP];	/* input/output buffer                   */
    float buf_float[N_SAMP];
    float Sn[M_PITCH];	/* float input speech samples            */
    float Sn_pre[M_PITCH];	/* pre-emphasised input speech samples   */
    COMP  Sw[FFT_ENC];	/* DFT of Sn[]                           */
    codec2_fft_cfg  fft_fwd_cfg;
    codec2_fftr_cfg  fftr_fwd_cfg;
    codec2_fftr_cfg  fftr_inv_cfg;
    float w[M_PITCH];	        /* time domain hamming window            */
    COMP  W[FFT_ENC];	/* DFT of w[]                            */
    MODEL model;
    float Pn[2*N_SAMP];	/* trapezoidal synthesis window          */
    float Sn_[2*N_SAMP];	/* synthesised speech */
    int   i,m;		/* loop variable                         */
    int   frames;
    float prev_Wo, prev__Wo, prev_uq_Wo;
    float pitch;
    char  out_file[MAX_STR];
    char  ampexp_arg[MAX_STR];
    char  phaseexp_arg[MAX_STR];
    float snr;
    float sum_snr;
    int orderi;
    int lpc_model = 0, order = LPC_ORD;
    int lsp = 0, lspd = 0, lspvq = 0;
    int lspres = 0;
    int lspjvm = 0, lspjnd = 0, lspmel = 0, lspmelvq = 0;
    #ifdef __EXPERIMENTAL__
    int lspanssi = 0,
    #endif
    int prede = 0;
    float pre_mem = 0.0, de_mem = 0.0;
    float ak[order];
    // COMP  Sw_[FFT_ENC];
    // COMP  Ew[FFT_ENC];

    int phase0 = 0;
    float ex_phase[MAX_AMP+1];

    int   postfilt;
    float bg_est = 0.0;

    int   hand_voicing = 0, phaseexp = 0, ampexp = 0, hi = 0, simlpcpf = 0, lspmelread = 0;
    int   lpcpf = 0;
    FILE *fvoicing = 0;
    FILE *flspmel = 0;

    MODEL prev_model;
    int dec;
    int decimate = 1;
    float lsps[order];
    float e, prev_e;
    int   lsp_indexes[order];
    float lsps_[order];
    float Woe_[2];

    float lsps_dec[4][LPC_ORD], e_dec[4], weight, weight_inc, ak_dec[4][LPC_ORD];
    MODEL model_dec[4], prev_model_dec;
    float prev_lsps_dec[order], prev_e_dec;

    void *nlp_states;
    float hpf_states[2];
    int   scalar_quant_Wo_e = 0;
    int   scalar_quant_Wo_e_low = 0;
    int   vector_quant_Wo_e = 0;
    int   dump_pitch_e = 0;
    FILE *fjvm = NULL;
    #ifdef DUMP
    int   dump;
    #endif
    struct PEXP *pexp = NULL;
    struct AEXP *aexp = NULL;
    float gain = 1.0;
    int   bpf_en = 0;
    int   bpfb_en = 0;
    float bpf_buf[BPF_N+N_SAMP];
    float lspmelvq_mse = 0.0;
    int   amread, Woread;
    FILE *fam = NULL, *fWo = NULL;
    int   hmread;
    FILE *fhm = NULL;

    char* opt_string = "ho:";
    struct option long_options[] = {
        { "lpc", required_argument, &lpc_model, 1 },
        { "lspjnd", no_argument, &lspjnd, 1 },
        { "lspmel", no_argument, &lspmel, 1 },
        { "lspmelread", required_argument, &lspmelread, 1 },
        { "lspmelvq", no_argument, &lspmelvq, 1 },
        { "lsp", no_argument, &lsp, 1 },
        { "lspd", no_argument, &lspd, 1 },
        { "lspvq", no_argument, &lspvq, 1 },
        { "lspres", no_argument, &lspres, 1 },
        { "lspjvm", no_argument, &lspjvm, 1 },
        #ifdef __EXPERIMENTAL__
        { "lspanssi", no_argument, &lspanssi, 1 },
        #endif
        { "phase0", no_argument, &phase0, 1 },
        { "phaseexp", required_argument, &phaseexp, 1 },
        { "ampexp", required_argument, &ampexp, 1 },
        { "postfilter", no_argument, &postfilt, 1 },
        { "hand_voicing", required_argument, &hand_voicing, 1 },
        { "dec", required_argument, &dec, 1 },
        { "hi", no_argument, &hi, 1 },
        { "simlpcpf", no_argument, &simlpcpf, 1 },
        { "lpcpf", no_argument, &lpcpf, 1 },
        { "prede", no_argument, &prede, 1 },
        { "dump_pitch_e", required_argument, &dump_pitch_e, 1 },
        { "sq_pitch_e", no_argument, &scalar_quant_Wo_e, 1 },
        { "sq_pitch_e_low", no_argument, &scalar_quant_Wo_e_low, 1 },
        { "vq_pitch_e", no_argument, &vector_quant_Wo_e, 1 },
        { "rate", required_argument, NULL, 0 },
        { "gain", required_argument, NULL, 0 },
        { "bpf", no_argument, &bpf_en, 1 },
        { "bpfb", no_argument, &bpfb_en, 1 },
        { "amread", required_argument, &amread, 1 },
        { "hmread", required_argument, &hmread, 1 },
        { "Woread", required_argument, &Woread, 1 },
        #ifdef DUMP
        { "dump", required_argument, &dump, 1 },
        #endif
        { "help", no_argument, NULL, 'h' },
        { NULL, no_argument, NULL, 0 }
    };
    int num_opts=sizeof(long_options)/sizeof(struct option);
    COMP Aw[FFT_ENC];
    COMP H[MAX_AMP];

    for(i=0; i<M_PITCH; i++) {
	Sn[i] = 1.0;
	Sn_pre[i] = 1.0;
    }
    for(i=0; i<2*N_SAMP; i++)
	Sn_[i] = 0;

    prev_uq_Wo = prev_Wo = prev__Wo = TWO_PI/P_MAX;

    prev_model.Wo = TWO_PI/P_MIN;
    prev_model.L = floor(PI/prev_model.Wo);
    for(i=1; i<=prev_model.L; i++) {
	prev_model.A[i] = 0.0;
	prev_model.phi[i] = 0.0;
    }
    for(i=1; i<=MAX_AMP; i++) {
	//ex_phase[i] = (PI/3)*(float)rand()/RAND_MAX;
	ex_phase[i] = 0.0;
    }
    e = prev_e = 1;
    hpf_states[0] = hpf_states[1] = 0.0;

    nlp_states = nlp_create(M_PITCH);

    if (argc < 2) {
        print_help(long_options, num_opts, argv);
    }

    /*----------------------------------------------------------------*\

                     Interpret Command Line Arguments

    \*----------------------------------------------------------------*/

    while(1) {
        int option_index = 0;
        int opt = getopt_long(argc, argv, opt_string,
                    long_options, &option_index);
        if (opt == -1)
            break;
        switch (opt) {
         case 0:
            if(strcmp(long_options[option_index].name, "lpc") == 0) {
                orderi = atoi(optarg);
                if((orderi < 4) || (orderi > order)) {
                    fprintf(stderr, "Error in LPC order (4 to %d): %s\n", order, optarg);
                    exit(1);
                }
                order = orderi;
            #ifdef DUMP
            } else if(strcmp(long_options[option_index].name, "dump") == 0) {
                if (dump)
	            dump_on(optarg);
            #endif
            } else if(strcmp(long_options[option_index].name, "lsp") == 0
                  || strcmp(long_options[option_index].name, "lspd") == 0
                  || strcmp(long_options[option_index].name, "lspvq") == 0) {
	        assert(order == LPC_ORD);
            } else if(strcmp(long_options[option_index].name, "dec") == 0) {

                decimate = atoi(optarg);
	        if ((decimate != 2) && (decimate != 3) && (decimate != 4)) {
	            fprintf(stderr, "Error in --dec, must be 2, 3, or 4\n");
	            exit(1);
	        }

                if (!phase0) {
                    printf("needs --phase0 to resample phase when using --dec\n");
                    exit(1);
                }
                if (!lpc_model) {
                    printf("needs --lpc [order] to resample amplitudes when using --dec\n");
                    exit(1);
                }

            } else if(strcmp(long_options[option_index].name, "hand_voicing") == 0) {
	        if ((fvoicing = fopen(optarg,"rt")) == NULL) {
	            fprintf(stderr, "Error opening voicing file: %s: %s.\n",
		        optarg, strerror(errno));
                    exit(1);
                }
            } else if(strcmp(long_options[option_index].name, "lspmelread") == 0) {
	        if ((flspmel = fopen(optarg,"rb")) == NULL) {
	            fprintf(stderr, "Error opening float lspmel file: %s: %s.\n",
		        optarg, strerror(errno));
                    exit(1);
                }
            } else if(strcmp(long_options[option_index].name, "Woread") == 0) {
	        if ((fWo = fopen(optarg,"rb")) == NULL) {
	            fprintf(stderr, "Error opening float Wo file: %s: %s.\n",
		        optarg, strerror(errno));
                    exit(1);
                }
            } else if(strcmp(long_options[option_index].name, "amread") == 0) {
	        if ((fam = fopen(optarg,"rb")) == NULL) {
	            fprintf(stderr, "Error opening float Am file: %s: %s.\n",
		        optarg, strerror(errno));
                    exit(1);
                }
            } else if(strcmp(long_options[option_index].name, "hmread") == 0) {
	        if ((fhm = fopen(optarg,"rb")) == NULL) {
	            fprintf(stderr, "Error opening float Hm file: %s: %s.\n",
		        optarg, strerror(errno));
                    exit(1);
                }
	    } else if(strcmp(long_options[option_index].name, "dump_pitch_e") == 0) {
	        if ((fjvm = fopen(optarg,"wt")) == NULL) {
	            fprintf(stderr, "Error opening pitch & energy dump file: %s: %s.\n",
		        optarg, strerror(errno));
                    exit(1);
                }
	    } else if(strcmp(long_options[option_index].name, "phaseexp") == 0) {
		strcpy(phaseexp_arg, optarg);
	    } else if(strcmp(long_options[option_index].name, "ampexp") == 0) {
		strcpy(ampexp_arg, optarg);
	    } else if(strcmp(long_options[option_index].name, "gain") == 0) {
		gain = atof(optarg);
	    } else if(strcmp(long_options[option_index].name, "rate") == 0) {
                if(strcmp(optarg,"3200") == 0) {
	            lpc_model = 1;
		    scalar_quant_Wo_e = 1;
	            lspd = 1;
	            phase0 = 1;
	            postfilt = 1;
	            decimate = 1;
		    lpcpf = 1;
               } else if(strcmp(optarg,"2400") == 0) {
	            lpc_model = 1;
		    vector_quant_Wo_e = 1;
	            lsp = 1;
	            phase0 = 1;
	            postfilt = 1;
	            decimate = 2;
		    lpcpf = 1;
               } else if(strcmp(optarg,"1400") == 0) {
	            lpc_model = 1;
		    vector_quant_Wo_e = 1;
	            lsp = 1;
	            phase0 = 1;
	            postfilt = 1;
	            decimate = 4;
 		    lpcpf = 1;
               } else if(strcmp(optarg,"1300") == 0) {
	            lpc_model = 1;
		    scalar_quant_Wo_e = 1;
	            lsp = 1;
	            phase0 = 1;
	            postfilt = 1;
	            decimate = 4;
 		    lpcpf = 1;
               } else if(strcmp(optarg,"1200") == 0) {
	            lpc_model = 1;
		    scalar_quant_Wo_e = 1;
	            lspjvm = 1;
	            phase0 = 1;
	            postfilt = 1;
	            decimate = 4;
 		    lpcpf = 1;
                } else {
                    fprintf(stderr, "Error: invalid output rate (3200|2400|1400|1200) %s\n", optarg);
                    exit(1);
                }
            }
            break;

         case 'h':
            print_help(long_options, num_opts, argv);
            break;

         case 'o':
	     if (strcmp(optarg, "-") == 0) fout = stdout;
	     else if ((fout = fopen(optarg,"wb")) == NULL) {
	        fprintf(stderr, "Error opening output speech file: %s: %s.\n",
		    optarg, strerror(errno));
	        exit(1);
	     }
	     strcpy(out_file,optarg);
	     break;

         default:
            /* This will never be reached */
            break;
        }
    }

    /* Input file */

    if (strcmp(argv[optind], "-")  == 0) fin = stdin;
    else if ((fin = fopen(argv[optind],"rb")) == NULL) {
	fprintf(stderr, "Error opening input speech file: %s: %s.\n",
		argv[optind], strerror(errno));
	exit(1);
    }

    ex_phase[0] = 0;
    Woe_[0] = Woe_[1] = 1.0;

    /*
      printf("lspd: %d lspdt: %d lspdt_mode: %d  phase0: %d postfilt: %d "
	   "decimate: %d dt: %d\n",lspd,lspdt,lspdt_mode,phase0,postfilt,
	   decimate,dt);
    */

    /* Initialise ------------------------------------------------------------*/

    fft_fwd_cfg = codec2_fft_alloc(FFT_ENC, 0, NULL, NULL); /* fwd FFT,used in several places   */
    fftr_fwd_cfg = codec2_fftr_alloc(FFT_ENC, 0, NULL, NULL); /* fwd FFT,used in several places   */
    fftr_inv_cfg = codec2_fftr_alloc(FFT_DEC, 1, NULL, NULL); /* inverse FFT, used just for synth */
    make_analysis_window(fft_fwd_cfg, w, W);
    make_synthesis_window(Pn);
    quantise_init();
    if (phaseexp)
	pexp = phase_experiment_create();
    if (ampexp)
	aexp = amp_experiment_create();

    if (bpfb_en)
        bpf_en = 1;
    if (bpf_en) {
        for(i=0; i<BPF_N; i++)
            bpf_buf[i] = 0.0;
    }

    for(i=0; i<LPC_ORD; i++) {
        prev_lsps_dec[i] = i*PI/(LPC_ORD+1);
    }
    prev_e_dec = 1;
    for(m=1; m<=MAX_AMP; m++)
	prev_model_dec.A[m] = 0.0;
    prev_model_dec.Wo = TWO_PI/P_MAX;
    prev_model_dec.L = PI/prev_model_dec.Wo;
    prev_model_dec.voiced = 0;

    /*----------------------------------------------------------------* \

                            Main Loop

    \*----------------------------------------------------------------*/

    frames = 0;
    sum_snr = 0;
    while(fread(buf,sizeof(short),N_SAMP,fin)) {
	frames++;

	for(i=0; i<N_SAMP; i++)
	    buf_float[i] = buf[i];

	/* optionally filter input speech */

        if (prede) {
           pre_emp(Sn_pre, buf_float, &pre_mem, N_SAMP);
           for(i=0; i<N_SAMP; i++)
                buf_float[i] = Sn_pre[i];
        }

        if (bpf_en) {
            /* filter input speech to create buf_float_bpf[], this is fed to the
               LPC modelling.  Unfiltered speech in in buf_float[], which is
               delayed to match that of the BPF */

            /* BPF speech */

            for(i=0; i<BPF_N; i++)
                bpf_buf[i] =  bpf_buf[N_SAMP+i];
            for(i=0; i<N_SAMP; i++)
                bpf_buf[BPF_N+i] = buf_float[i];
            if (bpfb_en)
                inverse_filter(&bpf_buf[BPF_N], bpfb, N_SAMP, buf_float, BPF_N);
            else
                inverse_filter(&bpf_buf[BPF_N], bpf, N_SAMP, buf_float, BPF_N);
        }

        /* shift buffer of input samples, and insert new samples */

	for(i=0; i<M_PITCH-N_SAMP; i++) {
	    Sn[i] = Sn[i+N_SAMP];
	}
	for(i=0; i<N_SAMP; i++) {
	    Sn[i+M_PITCH-N_SAMP] = buf_float[i];
        }

	/*------------------------------------------------------------*\

                      Estimate Sinusoidal Model Parameters

	\*------------------------------------------------------------*/

	nlp(nlp_states,Sn,N_SAMP,P_MIN,P_MAX,&pitch,Sw,W,&prev_uq_Wo);
	model.Wo = TWO_PI/pitch;

	dft_speech(fft_fwd_cfg, Sw, Sn, w);
	two_stage_pitch_refinement(&model, Sw);
	estimate_amplitudes(&model, Sw, W, 1);

        #ifdef DUMP
	dump_Sn(Sn); dump_Sw(Sw); dump_model(&model);
        #endif

	if (ampexp)
	    amp_experiment(aexp, &model, ampexp_arg);

	if (phaseexp) {
            #ifdef DUMP
	    dump_phase(&model.phi[0], model.L);
            #endif
	    phase_experiment(pexp, &model, phaseexp_arg);
            #ifdef DUMP
	    dump_phase_(&model.phi[0], model.L);
            #endif
	}

	if (hi) {
	    int m;
	    for(m=1; m<model.L/2; m++)
		model.A[m] = 0.0;
	    for(m=3*model.L/4; m<=model.L; m++)
		model.A[m] = 0.0;
	}

	/*------------------------------------------------------------*\

                            Zero-phase modelling

	\*------------------------------------------------------------*/

	if (phase0) {
	    float Wn[M_PITCH];		        /* windowed speech samples */
	    float Rk[order+1];                  /* autocorrelation coeffs  */
            COMP a[FFT_ENC];

            #ifdef DUMP
	    dump_phase(&model.phi[0], model.L);
            #endif

	    /* find aks here, these are overwritten if LPC modelling is enabled */

            for(i=0; i<M_PITCH; i++)
                Wn[i] = Sn[i]*w[i];
	    autocorrelate(Wn,Rk,M_PITCH,order);
	    levinson_durbin(Rk,ak,order);

	    /* determine voicing */

	    #if 0
	    snr = est_voicing_mbe(&model, Sw, W, Sw_, Ew);
            #else
	    snr = est_voicing_mbe(&model, Sw, W);
            #endif

	    if (dump_pitch_e)
		fprintf(fjvm, "%f %f %d ", model.Wo, snr, model.voiced);

	    //printf("snr %3.2f v: %d Wo: %f prev_Wo: %f\n", snr, model.voiced,
	    //	   model.Wo, prev_uq_Wo);
            #ifdef DUMP
	    //dump_Sw_(Sw_);
	    //dump_Ew(Ew);
	    dump_snr(snr);
            #endif

	    /* just to make sure we are not cheating - kill all phases */

	    for(i=0; i<=MAX_AMP; i++)
	    	model.phi[i] = 0;

            /* Determine DFT of A(exp(jw)), which is needed for phase0 model when
               LPC is not used, e.g. indecimate=1 (10ms) frames with no LPC */

            for(i=0; i<FFT_ENC; i++) {
                a[i].real = 0.0;
                a[i].imag = 0.0;
            }

            for(i=0; i<=order; i++)
                a[i].real = ak[i];
            codec2_fft(fft_fwd_cfg, a, Aw);

	    if (hand_voicing) {
		fscanf(fvoicing,"%d\n",&model.voiced);
	    }
	}

	/*------------------------------------------------------------*\

	        LPC model amplitudes and LSP quantisation

	\*------------------------------------------------------------*/

	if (lpc_model) {

            e = speech_to_uq_lsps(lsps, ak, Sn, w, order);
            for(i=0; i<LPC_ORD; i++)
                lsps_[i] = lsps[i];

            #ifdef DUMP
	    dump_ak(ak, order);
            dump_E(e);
            #endif

	    /* tracking down -ve energy values with BW expansion */
	    /*
	    if (e < 0.0) {
		int i;
		FILE*f=fopen("x.txt","wt");
		for(i=0; i<M_PITCH; i++)
		    fprintf(f,"%f\n", Sn[i]);
		fclose(f);
		printf("e = %f frames = %d\n", e, frames);
		for(i=0; i<order; i++)
		    printf("%f ", ak[i]);
		exit(0);
	    }
	    */

	    if (dump_pitch_e)
		fprintf(fjvm, "%f\n", e);

            #ifdef DUMP
            dump_lsp(lsps);
            #endif

	    /* various LSP quantisation schemes */

	    if (lsp) {
		encode_lsps_scalar(lsp_indexes, lsps, LPC_ORD);
		decode_lsps_scalar(lsps_, lsp_indexes, LPC_ORD);
		bw_expand_lsps(lsps_, LPC_ORD, 50.0, 100.0);
		lsp_to_lpc(lsps_, ak, LPC_ORD);
	    }

	    if (lspd) {
		encode_lspds_scalar(lsp_indexes, lsps, LPC_ORD);
		decode_lspds_scalar(lsps_, lsp_indexes, LPC_ORD);
		lsp_to_lpc(lsps_, ak, LPC_ORD);
	    }

#ifdef __EXPERIMENTAL__
	    if (lspvq) {
		lspvq_quantise(lsps, lsps_, LPC_ORD);
		bw_expand_lsps(lsps_, LPC_ORD, 50.0, 100.0);
		lsp_to_lpc(lsps_, ak, LPC_ORD);
	    }
#endif

	    if (lspjvm) {
		/* Jean-Marc's multi-stage, split VQ */
		lspjvm_quantise(lsps, lsps_, LPC_ORD);
		{
		    float lsps_bw[LPC_ORD];
		    memcpy(lsps_bw, lsps_, sizeof(float)*LPC_ORD);
		    bw_expand_lsps(lsps_bw, LPC_ORD, 50.0, 100.0);
		    lsp_to_lpc(lsps_bw, ak, LPC_ORD);
		}
	    }

#ifdef __EXPERIMENTAL__
	    if (lspanssi) {
		/*  multi-stage VQ from Anssi Ramo OH3GDD */

		lspanssi_quantise(lsps, lsps_, LPC_ORD, 5);
		bw_expand_lsps(lsps_, LPC_ORD, 50.0, 100.0);
		lsp_to_lpc(lsps_, ak, LPC_ORD);
	    }
#endif

	    /* experimenting with non-linear LSP spacing to see if
	       it's just noticable */

	    if (lspjnd) {
		for(i=0; i<LPC_ORD; i++)
		    lsps_[i] = lsps[i];
		locate_lsps_jnd_steps(lsps_, LPC_ORD);
		lsp_to_lpc(lsps_, ak, LPC_ORD);
	    }

	    /* Another experiment with non-linear LSP spacing, this
	       time using a scaled version of mel frequency axis
	       warping.  The scaling is such that the integer output
	       can be directly sent over the channel.
	    */

	    if (lspmel) {
		float f, f_;
		float mel[order];
		int   mel_indexes[order];

		for(i=0; i<order; i++) {
		    f = (4000.0/PI)*lsps[i];
		    mel[i] = floor(2595.0*log10(1.0 + f/700.0) + 0.5);
		}

                #define MEL_ROUND 25
		for(i=1; i<order; i++) {
		    if (mel[i] <= mel[i-1]+MEL_ROUND) {
			mel[i]+=MEL_ROUND/2;
			mel[i-1]-=MEL_ROUND/2;
                        i = 1;
                    }
		}

                #ifdef DUMP
                dump_mel(mel, order);
                #endif

 		encode_mels_scalar(mel_indexes, mel, 6);
                #ifdef DUMP
                dump_mel_indexes(mel_indexes, 6);
                #endif
		//decode_mels_scalar(mel, mel_indexes, 6);

                /* read in VQed lsp-mels from octave/melvq.m */

                if (lspmelread) {
                    float mel_[order];
                    int ret = fread(mel_, sizeof(float), order, flspmel);
                    assert(ret == order);
                    for(i=0; i<order; i++) {
                        lspmelvq_mse += pow(mel[i] - mel_[i], 2.0);
                        mel[i] = mel_[i];
                    }
                }

                if (lspmelvq) {
                    int indexes[3];
                    //lspmelvq_mse += lspmelvq_quantise(mel, mel, order);
                    lspmelvq_mse += lspmelvq_mbest_encode(indexes, mel, mel, order, 5);
                }

                /* ensure no unstable filters after quantisation */

                #define MEL_ROUND 25
		for(i=1; i<order; i++) {
		    if (mel[i] <= mel[i-1]+MEL_ROUND) {
			mel[i]+=MEL_ROUND/2;
			mel[i-1]-=MEL_ROUND/2;
                        i = 1;
                    }
		}

		for(i=0; i<order; i++) {
		    f_ = 700.0*( pow(10.0, mel[i]/2595.0) - 1.0);
		    lsps_[i] = f_*(PI/4000.0);
		}

		lsp_to_lpc(lsps_, ak, order);

	    }

	    if (scalar_quant_Wo_e) {

		e = decode_energy(encode_energy(e, E_BITS), E_BITS);
                model.Wo = decode_Wo(encode_Wo(model.Wo, WO_BITS), WO_BITS);
		model.L  = PI/model.Wo; /* if we quantise Wo re-compute L */
	    }

	    if (scalar_quant_Wo_e_low) {
                int ind;
		e = decode_energy(ind = encode_energy(e, 3), 3);
                model.Wo = decode_log_Wo(encode_log_Wo(model.Wo, 5), 5);
		model.L  = PI/model.Wo; /* if we quantise Wo re-compute L */
	    }

	    if (vector_quant_Wo_e) {

		/* JVM's experimental joint Wo & LPC energy quantiser */

		quantise_WoE(&model, &e, Woe_);
	    }

	}

        if (amread) {
            int ret = fread(model.A, sizeof(float), MAX_AMP, fam);
            assert(ret == MAX_AMP);
        }

        if (Woread) {
            int ret = fread(&model.Wo, sizeof(float), 1, fWo);
            model.L = floor(PI/model.Wo);
            assert(ret == 1);
        }

	/*------------------------------------------------------------*\

          Synthesise and optional decimation to 20 or 40ms frame rate

	\*------------------------------------------------------------*/

        /*
           if decimate == 2, we interpolate frame n from frame n-1 and n+1
           if decimate == 4, we interpolate frames n, n+1, n+2, from frames n-1 and n+3

           This is meant to give identical results to the implementations of various modes
           in codec2.c
        */

        /* delay line to keep frame by frame voicing decisions */

        for(i=0; i<decimate-1; i++)
            model_dec[i] = model_dec[i+1];
        model_dec[decimate-1] = model;

        if ((frames % decimate) == 0) {
            for(i=0; i<order; i++)
                lsps_dec[decimate-1][i] = lsps_[i];
            e_dec[decimate-1] = e;
            model_dec[decimate-1] = model;

            /* interpolate the model parameters */

            weight_inc = 1.0/decimate;
            for(i=0, weight=weight_inc; i<decimate-1; i++, weight += weight_inc) {
                //model_dec[i].voiced = model_dec[decimate-1].voiced;
                interpolate_lsp_ver2(&lsps_dec[i][0], prev_lsps_dec, &lsps_dec[decimate-1][0], weight, order);
                interp_Wo2(&model_dec[i], &prev_model_dec, &model_dec[decimate-1], weight);
                e_dec[i] = interp_energy2(prev_e_dec, e_dec[decimate-1],weight);
            }

            /* then recover spectral amplitudes and synthesise */

            for(i=0; i<decimate; i++) {
                if (lpc_model) {
                    lsp_to_lpc(&lsps_dec[i][0], &ak_dec[i][0], order);
                    aks_to_M2(fftr_fwd_cfg, &ak_dec[i][0], order, &model_dec[i], e_dec[i],
                              &snr, 0, simlpcpf, lpcpf, 1, LPCPF_BETA, LPCPF_GAMMA, Aw);
                    apply_lpc_correction(&model_dec[i]);
                    sum_snr += snr;
                    #ifdef DUMP
                    dump_lsp_(&lsps_dec[i][0]);
                    dump_ak_(&ak_dec[i][0], order);
                    dump_quantised_model(&model_dec[i]);
                    #endif
                }

                /* optionally read in Aw FFT vector, we really only care about the phase
                   of each entry, used for reading in phases generated by Octave */

                if (hmread) {
                    int ret = fread(&H, sizeof(float), MAX_AMP, fhm);
                    assert(ret == MAX_AMP);
                }

                //fprintf(stderr, "frame: %d Wo: %f L: %d v: %d\n", frames, model_dec[i].Wo, model_dec[i].L,  model_dec[i].voiced);
                if (phase0) {
                    sample_phase(&model_dec[i], H, Aw);
                    phase_synth_zero_order(&model_dec[i], ex_phase, H);
                }
                if (postfilt)
                    postfilter(&model_dec[i], &bg_est);
                synth_one_frame(fftr_inv_cfg, buf, &model_dec[i], Sn_, Pn, prede, &de_mem, gain);
                if (fout != NULL) fwrite(buf,sizeof(short),N_SAMP,fout);
            }

            /* update memories for next frame ----------------------------*/

            prev_model_dec = model_dec[decimate-1];
            prev_e_dec = e_dec[decimate-1];
            for(i=0; i<LPC_ORD; i++)
                prev_lsps_dec[i] = lsps_dec[decimate-1][i];
       }

    }

    /*----------------------------------------------------------------*\

                            End Main Loop

    \*----------------------------------------------------------------*/

    fclose(fin);

    if (fout != NULL)
	fclose(fout);

    if (lpc_model) {
    	fprintf(stderr, "SNR av = %5.2f dB\n", sum_snr/frames);
        if (lspmelvq || lspmelread)
            fprintf(stderr, "lspmelvq std = %3.1f Hz\n", sqrt(lspmelvq_mse/frames));
    }

    if (phaseexp)
	phase_experiment_destroy(pexp);
    if (ampexp)
	amp_experiment_destroy(aexp);
    #ifdef DUMP
    if (dump)
	dump_off();
    #endif

    if (hand_voicing)
	fclose(fvoicing);

    nlp_destroy(nlp_states);

    return 0;
}

void synth_one_frame(codec2_fftr_cfg fftr_inv_cfg, short buf[], MODEL *model, float Sn_[],
                     float Pn[], int prede, float *de_mem, float gain)
{
    int     i;

    synthesise(fftr_inv_cfg, Sn_, model, Pn, 1);
    if (prede)
        de_emp(Sn_, Sn_, de_mem, N_SAMP);

    for(i=0; i<N_SAMP; i++) {
	Sn_[i] *= gain;
	if (Sn_[i] > 32767.0)
	    buf[i] = 32767;
	else if (Sn_[i] < -32767.0)
	    buf[i] = -32767;
	else
	    buf[i] = Sn_[i];
    }

}

void print_help(const struct option* long_options, int num_opts, char* argv[])
{
	int i;
	char *option_parameters;

	fprintf(stderr, "\nCodec2 - low bit rate speech codec - Simulation Program\n"
		"\thttp://rowetel.com/codec2.html\n\n"
		"usage: %s [OPTIONS] <InputFile>\n\n"
                "Options:\n"
                "\t-o <OutputFile>\n", argv[0]);
        for(i=0; i<num_opts-1; i++) {
		if(long_options[i].has_arg == no_argument) {
			option_parameters="";
		} else if (strcmp("lpc", long_options[i].name) == 0) {
			option_parameters = " <Order>";
		} else if (strcmp("dec", long_options[i].name) == 0) {
			option_parameters = " <2|4>";
		} else if (strcmp("hand_voicing", long_options[i].name) == 0) {
			option_parameters = " <VoicingFile>";
		} else if (strcmp("dump_pitch_e", long_options[i].name) == 0) {
			option_parameters = " <Dump File>";
		} else if (strcmp("rate", long_options[i].name) == 0) {
			option_parameters = " <3200|2400|1400|1300|1200>";
		} else if (strcmp("dump", long_options[i].name) == 0) {
			option_parameters = " <DumpFilePrefix>";
		} else {
			option_parameters = " <UNDOCUMENTED parameter>";
		}
		fprintf(stderr, "\t--%s%s\n", long_options[i].name, option_parameters);
	}

	exit(1);
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: ear_protection()
  AUTHOR......: David Rowe
  DATE CREATED: Nov 7 2012

  Limits output level to protect ears when there are bit errors or the input
  is overdriven.  This doesn't correct or mask bit errors, just reduces the
  worst of their damage.

\*---------------------------------------------------------------------------*/

static void ear_protection(float in_out[], int n) {
    float max_sample, over, gain;
    int   i;

    /* find maximum sample in frame */

    max_sample = 0.0;
    for(i=0; i<n; i++)
        if (in_out[i] > max_sample)
            max_sample = in_out[i];

    /* determine how far above set point */

    over = max_sample/30000.0;

    /* If we are x dB over set point we reduce level by 2x dB, this
       attenuates major excursions in amplitude (likely to be caused
       by bit errors) more than smaller ones */

    if (over > 1.0) {
        gain = 1.0/(over*over);
        //fprintf(stderr, "gain: %f\n", gain);
        for(i=0; i<n; i++)
            in_out[i] *= gain;
    }
}

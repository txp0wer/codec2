/*---------------------------------------------------------------------------*\

  FILE........: codec2_internal.h
  AUTHOR......: David Rowe
  DATE CREATED: April 16 2012

  Header file for Codec2 internal states, exposed via this header
  file to assist in testing.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2012 David Rowe

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

#ifndef __CODEC2_INTERNAL__
#define __CODEC2_INTERNAL__

#include "codec2_fft.h"

struct CODEC2 {
    int           mode;
    codec2_fft_cfg  fft_fwd_cfg;             /* forward FFT config                        */
    codec2_fftr_cfg fftr_fwd_cfg;            /* forward real FFT config                   */
#ifdef CODEC2_WIDEBAND
    float         w[M_PITCH_WB];	                   /* time domain hamming window                */
#else
    float         w[M_PITCH];              /* time domain hamming window                */
#endif
    COMP          W[FFT_ENC];	           /* DFT of w[]                                */
#ifdef CODEC2_WIDEBAND
    float         Pn[2*N_SAMP_WB];         /* trapezoidal synthesis window              */
#else
    float         Pn[2*N_SAMP];	           /* trapezoidal synthesis window              */
#endif
    float        *bpf_buf;                 /* buffer for band pass filter               */
#ifdef CODEC2_WIDEBAND
    float         Sn[M_PITCH_WB];                   /* input speech                              */
#else
    float         Sn[M_PITCH];                   /* input speech                              */
#endif
    float         hpf_states[2];           /* high pass filter states                   */
    void         *nlp;                     /* pitch predictor states                    */
    int           gray;                    /* non-zero for gray encoding                */

    codec2_fftr_cfg  fftr_inv_cfg;             /* inverse FFT config                        */
#ifdef CODEC2_WIDEBAND
    float         Sn_[2*N_SAMP_WB];        /* synthesised output speech                 */
#else
    float         Sn_[2*N_SAMP];	           /* synthesised output speech                 */
#endif
    float         ex_phase;                /* excitation model phase track              */
    float         bg_est;                  /* background noise estimate for post filter */
    float         prev_Wo_enc;             /* previous frame's pitch estimate           */
    MODEL         prev_model_dec;          /* previous frame's model parameters         */
    float         prev_lsps_dec[LPC_ORD];  /* previous frame's LSPs                     */
    float         prev_e_dec;              /* previous frame's LPC energy               */

    int           lpc_pf;                  /* LPC post filter on                        */
    int           bass_boost;              /* LPC post filter bass boost                */
    float         beta;                    /* LPC post filter parameters                */
    float         gamma;

    float         xq_enc[2];               /* joint pitch and energy VQ states          */
    float         xq_dec[2];

    int           smoothing;               /* enable smoothing for channels with errors */
    float        *softdec;                 /* optional soft decn bits from demod        */
};

// test and debug
void analyse_one_frame(struct CODEC2 *c2, MODEL *model, short speech[]);
void synthesise_one_frame(struct CODEC2 *c2, short speech[], MODEL *model,
			  COMP Aw[]);
#endif

%
% fmfsk.m
% Author: Brady O'Brien 3 Feb 2016
%   Copyright 2016 David Rowe
%  
%  All rights reserved.
%
%  This program is free software; you can redistribute it and/or modify
%  it under the terms of the GNU Lesser General Public License veRbion 2, as
%  published by the Free Software Foundation.  This program is
%  distributed in the hope that it will be useful, but WITHOUT ANY
%  WARRANTY; without even the implied warranty of MERCHANTABILITY or
%  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
%  License for more details.
%
%  You should have received a copy of the GNU Lesser General Public License
%  along with this program; if not, see <http://www.gnu.org/licenses/>.

% mancyfsk.m modem, extracted and made suitable for C implementation

fm;
pkg load signal;
pkg load parallel;
1;

% Init fmfsk modem
%Fs is sample frequency
%Rb is pre-manchester bit rate
function states = fmfsk_init(Fs,Rb)
    assert(floor(Fs/(Rb*2))==(Fs/(Rb*2)));
    assert(mod(Fs,Rb*2)==0);
    
    states.Rb = Rb;
    states.Rs = Rb*2;   % Manchester-encoded bitrate
    states.Fs = Fs;
    states.Ts = Fs/states.Rs;
    % Processing buffer size. about 40ms here
    states.N = floor(states.Rs*.080)*states.Ts;     
    states.nin = states.N;          % Samples in the next demod cycle
    states.nstash = states.Ts*2;    % How many samples to stash away between proc cycles for timing adjust
    states.nmem =  states.N+(6*states.Ts);
    states.nsym = floor(states.Rs*.080);
    states.nbit = floor(states.Rb*.080)
    %Old sample memory
    %states.oldsamps = zeros(1,states.nstash);
    
    states.oldsamps = zeros(1,states.nmem);
    
    %Last sampled-stream output, for odd bitstream generation
    states.lastint = 0;
    
    %Some stats
    states.norm_rx_timing = 0;
    
endfunction

%Generate a stream of manchester-coded bits to be sent
% to any ordinary FM modulator or VCO or something
function tx = fmfsk_mod(states,inbits)
    Ts = states.Ts;
    tx = zeros(1,length(inbits)*2);
    for ii = 1:length(inbits)
        st = 1 + (ii-1)*Ts*2;
        md = st+Ts-1;
        en = md+Ts;
        if inbits(ii)==0
            tx(st:md)   = -ones(1,Ts);
            tx(md+1:en) =  ones(1,Ts);
        else
            tx(st:md)   =  ones(1,Ts);
            tx(md+1:en) = -ones(1,Ts);
        end
    end
endfunction

%Demodulate a bag of bits from the output of an FM demodulator
% This function produces nbits output bits and takes states.nin samples
function [rx_bits states] = fmfsk_demod(states,rx)
    Ts = states.Ts;
    Fs = states.Fs;
    Rs = states.Rs;
    nin = states.nin;
    N = states.N;
    nsym = states.nsym;
    nbits = states.nsym/2;
    nmem = states.nmem;
    nstash = states.nstash;
    
    nold = nmem-nin;
    ssamps = states.oldsamps;
    
    
    %Shift in nin samples
    ssamps(1:nold) = ssamps(nmem-nold+1:nmem);
    ssamps(nold+1:nmem) = rx;
    states.oldsamps = ssamps;
    
    rx_filt = zeros(1,nsym*Ts);
    %Integrate Ts input samples at every offset
    %This is the same thing as filtering with a filter of all ones
    % out to Ts.
    % It's implemented like this for ease of C-porting
    for ii=(1:(nsym+1)*Ts)
        st = ii;
        en = st+Ts-1;
        rx_filt(ii) = sum(ssamps(st:en));
    end
 
    % Fine timing estimation ------------------------------------------------------

    % Estimate fine timing using line at Rs/2 that Manchester encoding provides
    % We need this to sync up to Manchester codewords. 
    Np = length(rx_filt);
    w = 2*pi*(Rs)/Fs;
    x = (rx_filt .^ 2) * exp(-j*w*(0:Np-1))';
    norm_rx_timing = angle(x)/(2*pi) - 0.42
    rx_timing = round(norm_rx_timing*Ts)
    
    %If rx timing is too far out, ask for more or less sample the next time
    % around to even it all out
    next_nin = N;
    if norm_rx_timing > .5;
       next_nin += Ts/2;
    end
    if norm_rx_timing < -.5;
       next_nin -= Ts/2;
    end

    states.nin = next_nin;
    
    % Sample rx_filt at the optimum inst, as figured by rx_timing
    rx_symsamp = rx_filt((Ts/2)+Ts+rx_timing:Ts:length(rx_filt));
    length(rx_symsamp)
    
    figure(1);
    plot(rx_filt);
    figure(2);
    stairs(rx_symsamp);
    
    %'Even' and 'Odd' manchester bitstream.
    % We'll figure out which to produce later
    rx_even = zeros(1,nbits);
    rx_odd = zeros(1,nbits);
    apeven = 0;
    apodd = 0;
    oss = states.lastint;
    
    % Figure first 'odd' bit from the last sampled output and first new sampled output
    mediff = oss-rx_symsamp(1);
    rx_odd(1) = mediff>0;
    
    % Note: ii is a zero-indexed array pointer, for less mind-breaking c portage
    for ii = (1:nsym-2)
        mediff = rx_symsamp(ii+1)-rx_symsamp(ii+1+1);
        mebit = mediff>0;
        
        if mod(ii,2)==0
            apeven += abs(mediff);
            rx_even( floor(ii/2)+1 ) = mebit;
        else
            apodd += abs(mediff);
            rx_odd(   floor(ii/2)+2 ) = mebit;
        end
    end
    % Decide on the correct ME alignment
    if(apeven>apodd)
        rx_bits = rx_even;
    else
        rx_bits = rx_odd;
    end
    
    states.lastint = rx_symsamp(nsym);
endfunction

% run_sim copypasted from fsk_horus.m
% simulation of tx and rx side, add noise, channel impairments ----------------------

function fmfsk_run_sim(EbNodB,timing_offset,fading,df,dA)
  test_frame_mode = 2;
  frames = 70;
  %EbNodB = 3;
  %timing_offset = 0.0; % see resample() for clock offset below
  %fading = 0;          % modulates tx power at 2Hz with 20dB fade depth, 
                       % to simulate balloon rotating at end of mission
  df     = 0;          % tx tone freq drift in Hz/s
  dA     = 1;          % amplitude imbalance of tones (note this affects Eb so not a gd idea)

  more off
  rand('state',1); 
  randn('state',1);
  
  Fs = 48000;
  Rbit = 2400;

  % ----------------------------------------------------------------------

  fm_states.pre_emp = 0;
  fm_states.de_emp  = 0;
  fm_states.Ts      = Fs/(Rbit*2);
  fm_states.Fs      = Fs; 
  fm_states.fc      = Fs/4; 
  fm_states.fm_max  = 3E3;
  fm_states.fd      = 5E3;
  fm_states.output_filter = 0;
  fm_states = analog_fm_init(fm_states);

  % ----------------------------------------------------------------------
  
  states = fmfsk_init(Fs,Rbit);

  states.verbose = 0x1;
  Rs = states.Rs;
  nsym = states.nsym;
  Fs = states.Fs;
  nbit = states.nbit;

  EbNo = 10^(EbNodB/10);
  variance = states.Fs/(states.Rb*EbNo);

  % set up tx signal with payload bits based on test mode

  if test_frame_mode == 1
     % test frame of bits, which we repeat for convenience when BER testing
    test_frame = round(rand(1, states.nbit));
    tx_bits = [];
    for i=1:frames+1
      tx_bits = [tx_bits test_frame];
    end
  end
  if test_frame_mode == 2
    % random bits, just to make sure sync algs work on random data
    tx_bits = round(rand(1, states.nbit*(frames+1)));
  end
  if test_frame_mode == 3
    % repeating sequence of all symbols
    % great for initial test of demod if nothing else works, 
    % look for this pattern in rx_bits

       % ...10101...
      tx_bits = zeros(1, states.nbit*(frames+1));
      tx_bits(1:2:length(tx_bits)) = 1;

  end

  [b, a] = cheby1(4, 1, 300/Fs, 'high');   % 300Hz HPF to simulate FM radios
  tx_pmod = fmfsk_mod(states, tx_bits);
  figure(10)
  plot(tx_pmod);
  tx = analog_fm_mod(fm_states, tx_pmod);
  %tx = fsk_horus_mod(states, tx_bits);

  if(timing_offset>0)
    tx = resample(tx, 1000, 1001); % simulated 1000ppm sample clock offset
  end
  
  if fading
     ltx = length(tx);
     tx = tx .* (1.1 + cos(2*pi*2*(0:ltx-1)/Fs))'; % min amplitude 0.1, -20dB fade, max 3dB
  end

  noise = sqrt(variance)*randn(length(tx),1);
  size(tx)
  size(noise)
  rx    = tx + noise';
  
  %Demod by analog fm
  rx    = analog_fm_demod(fm_states, rx);
  
  
  %rx = real(rx);
  %b1 = fir2(100, [0 4000 5200 48000]/48000, [1 1 0.5 0.5]);
  %rx = filter(b1,1,rx);
  %[b a] = cheby2(6,40,[3000 6000]/(Fs/2));
  %rx = filter(b,a,rx);
  %rx = sign(rx);
  %rx(find (rx > 1)) = 1;
  %rx(find (rx < -1)) = -1;

  % dump simulated rx file

  timing_offset_samples = round(timing_offset*states.Ts);
  st = 1 + timing_offset_samples;
  rx_bits_buf = zeros(1,2*nbit);
  x_log = [];
  timing_nl_log = [];
  norm_rx_timing_log = [];
  f_int_resample_log = [];
  f_log = [];
  EbNodB_log = [];
  rx_bits_log = [];
  rx_bits_sd_log = [];

  for f=1:frames

    % extract nin samples from input stream

    nin = states.nin;
    en = st + states.nin - 1;
    sf = rx(st:en);
    st += nin;

    % demodulate to stream of bits

    [rx_bits states] = fmfsk_demod(states, sf);

    rx_bits_buf(1:nbit) = rx_bits_buf(nbit+1:2*nbit);
    rx_bits_buf(nbit+1:2*nbit) = rx_bits;
    rx_bits_log = [rx_bits_log rx_bits];
    %rx_bits_sd_log = [rx_bits_sd_log states.rx_bits_sd];

    %norm_rx_timing_log = [norm_rx_timing_log states.norm_rx_timing];
    %x_log = [x_log states.x];
    %timing_nl_log = [timing_nl_log states.timing_nl];
    %f_int_resample_log = [f_int_resample_log abs(states.f_int_resample(:,:))];
    %f_log = [f_log; states.f];
    %EbNodB_log = [EbNodB_log states.EbNodB];

  end

  ber = 1;
  ox = 1;
  rx_bits = rx_bits_log;
  bitcnt = length(tx_bits);
  for offset = (1:100)
    nerr = sum(xor(rx_bits(offset:length(rx_bits)),tx_bits(1:length(rx_bits)+1-offset)));
    bern = nerr/(bitcnt-offset);
    if(bern < ber)
      ox = offset;
      best_nerr = nerr;
    end
    ber = min([ber bern]);
  end
  offset = ox;
  figure(3);
  plot(xor(rx_bits(ox:length(rx_bits)),tx_bits(1:length(rx_bits)+1-ox)))
  
  ber
  
 endfunction


% demodulate a file of 8kHz 16bit short samples --------------------------------




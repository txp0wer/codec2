#!/bin/bash
# File: $(BIN)/codec2
# Version: 0.1
# License: same as the codec2 library + tools
# Copyright: 2017 by txp0wer
# Decription: Wrapper around the codec2 command line tools
# Requires: codec2-tools, sox

# i'm too lazy to implement command line parsing
[[ "$bitrate" ]] || bitrate=1200 # minimum bitrate for stable version
[[ "$lowpass" ]] || lowpass=200 # not sure if we really need this, but doesn't do any harm
[[ "$error_rate" ]] || error_rate=0
[[ "$samplerate" ]] || samplerate=8000

# dynamic range compression - on my default might be a good idea
# speech files that don't always have full "loudness" are better comprehensible after de-/coding with this
if [[ "$dyn_comp" == auto ]] 
then compand="compand 0.3,1 6:-70,-60,-20 -5 -90 0.2 vol -1dB" # parameters taken from the sox manpage; -1dB against too bad clipping
else compand=""
fi

case "$1" in
  enc)
    sox "$2" -r "$samplerate" -c 1 -t s16 --endian little /dev/stdout sinc "$lowpass" $filter $compand |
      c2enc "$bitrate" /dev/stdin "$3"
  ;;
  dec)
    c2dec "$bitrate" "$2" /dev/stdout --ber "0.$(printf %03d "$error_rate")"|
      sox -r "$samplerate" -c 1 -t s16 --endian little /dev/stdin "$3"
  ;;
  rec) 
    rec -r "$samplerate" -c 1 -t s16 --endian little /dev/stdout sinc "$lowpass" $filter $compand |
      c2enc "$bitrate" /dev/stdin "$3"
  ;;
  play)
    c2dec "$bitrate" "$2" /dev/stdout --ber "0.$(printf %03d "$error_rate")"|
      play -r "$samplerate" -c 1 -t s16 --endian little /dev/stdin
  ;;
  test) # encode, then decode and play, in order to check if it's still comprehensible
  if [[ "$2" ]]
  then sox "$2" -r "$samplerate" -c 1 -t s16 --endian little /dev/stdout sinc "$lowpass" $filter $compand
  else rec -r "$samplerate" -c 1 -t s16 --endian little /dev/stdout sinc "$lowpass" $filter $compand
  fi |
    c2enc "$bitrate" /dev/std{in,out} |
      c2dec "$bitrate" /dev/std{in,out} --ber "0.$(printf %03d "$error_rate")"|
        play -r "$samplerate" -c 1 -t s16 --endian little /dev/stdin
  ;;
  *)
    echo "Usage: [samplerate=<hz>] [filter=<sox_filter>] [error_rate={0..999}] [bitrate={437.5B|437.5|700B|700|750|812.5|875|1000|1200|1300|1400|1500|1600|2000|2400|3200}] [lowpass=<freq>] [dyn_comp=off] $0 {enc|dec|rec|test} <infile> [<outfile>]" >>/dev/stderr
    exit 1
  ;;
esac

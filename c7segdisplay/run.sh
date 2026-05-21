#!/bin/bash
echo "Waiting for 5 seconds to cool the drinks.."
sleep 1
echo "4.."
sleep 1
echo "3.."
sleep 1
echo "2.."
sleep 1
echo "1.."
sleep 1

if ! pgrep -x mp4player > /dev/null; then
	trap 'kill $P1 $P2' INT TERM
	./mp4player --right ./video.mp4 & P1=$!
	./seg7 --left & P2=$!
	wait
fi

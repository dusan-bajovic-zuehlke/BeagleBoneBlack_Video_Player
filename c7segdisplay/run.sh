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
	SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
	trap 'kill $P1 $P2' INT TERM
	$SCRIPT_DIR/mp4player --right $SCRIPT_DIR/video.mp4 & P1=$!
	$SCRIPT_DIR/seg7 --left & P2=$!
	wait
fi

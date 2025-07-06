#!/bin/bash

make

mkdir -p store
for i in {1..9}; do
	# cp ss testdir/$i
	mkdir -p store/$i
	cp ss store/$i
done

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <IP> <PORT>"
  echo "Enter the naming server's details."
  exit 1
fi

IP=$1
PORT=$2

SESSION_NAME="9-storage-servers"
DIR_NAME="store"

# Check if the session already exists
tmux has-session -t $SESSION_NAME 2>/dev/null

if [ $? -ne 0 ]; then
  # Create a new session and split the first window into 9 panes
  tmux new-session -d -s $SESSION_NAME
  for i in {1..8}; do
    tmux split-window -t $SESSION_NAME
    tmux select-layout -t $SESSION_NAME tiled
  done
fi

# Run the command in each pane
for i in {1..9}; do
  tmux send-keys -t $SESSION_NAME.$i "cd $DIR_NAME/$i" C-m
  # tmux send-keys -t $SESSION_NAME.$i "open ." C-m
  # tmux send-keys -t $SESSION_NAME.$i "./ss $IP $PORT" C-m
done

# Attach to the session
tmux attach-session -t $SESSION_NAME
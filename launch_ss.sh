#!/bin/bash

IP=$1
PORT=$2

SESSION_NAME="9-storage-servers"
DIR_NAME="testdir"
# CMD="echo $i" # Replace with the command you want to run

# Check if the session already exists
tmux has-session -t $SESSION_NAME 2>/dev/null

if [ $? != 0 ]; then
  # Create a new session and split the first window into 9 panes
  tmux new-session -d -s $SESSION_NAME
  for i in {1..8}; do
    tmux split-window -t $SESSION_NAME
    tmux select-layout -t $SESSION_NAME tiled
  done
fi

# Run the command in each pane
for i in $(seq 0 8); do
  tmux send-keys -t $SESSION_NAME.$i "cd $DIR_NAME/$((i + 1))" C-m
  tmux send-keys -t $SESSION_NAME.$i "./ss $IP $PORT" C-m
done

# Attach to the session
tmux attach-session -t $SESSION_NAME
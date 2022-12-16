#!/bin/bash
runtime=30
count=4

echo "------ Figure 5 Evaluation Start ------"

cd ae-micro-polyjuice
echo "Micro EA-Polyjuice"
for((i=1;i<=$count;i++));
do
    threads=10
    warehouse=1
    echo "tpc-c. training_method=EA-Polyjuice. num-threads=$threads, warehouse-num=$warehouse"
    python training/ERL_main.py \
        --graph \
        --workload-type tpcc \
        --bench-opt "-w 45,43,4,4,4" \
        --scale-factor $warehouse \
        --max-iteration 3 \
        --eval-time 1.0 \
        --psize 5 \
        --random-branch 4 \
        --mutate-rate 0.05 \
        --nworkers $threads \
        --pickup-policy training/input-RL-ic3-micro.txt \
        --expr-name "EA-Polyjuice-"
done
cd ..


echo "------ Micro Train Finish ------"

#!/bin/bash
runtime=30
count=2

echo "------ Figure 8(b) Evaluation Start ------"

cd ae-tpce-polyjuice
# make dbtest -j
# echo "------ Make Done ------"
echo "tpc-e figure8(b) Polyjuice"
for threads in {1,2,4,8,12,16,32,48}
do
    for((i=1;i<=$count;i++));
    do
        theta=3.0
        echo "tpc-e. cc=Polyjuice. num-threads=$threads, zipf_theta=$theta"
        policy="../ae-policy/tpce/${threads}th-${theta}theta.txt"
        ./out-perf.masstree/benchmarks/dbtest \
            --bench tpce \
            --parallel-loading \
            --retry-aborted-transactions \
            --bench-opt "-w 0,0,0,0,0,0,50,0,0,50 -m 0 -s 1 -a $theta" \
            --db-type ndb-ic3 \
            --backoff-aborted-transactions \
            --runtime $runtime \
            --num-threads $threads \
            --scale-factor 1 \
            --policy $policy
    done
done
cd ..

# cd ae-tpce-silo
# # make dbtest -j
# # echo "------ Make Done ------"
# echo "tpc-e figure8(b) Silo"
# for threads in {1,2,4,8,12,16,32,48}
# do
#     for((i=1;i<=$count;i++));
#     do
#         theta=3.0
#         echo "tpc-e. cc=Silo. num-threads=$threads, zipf_theta=$theta"
#         ./out-perf.masstree/benchmarks/dbtest \
#             --bench tpce \
#             --parallel-loading \
#             --retry-aborted-transactions \
#             --bench-opt "-w 0,0,0,0,0,0,50,0,0,50 -m 0 -s 1 -a $theta" \
#             --db-type ndb-proto2 \
#             --backoff-aborted-transactions \
#             --runtime $runtime \
#             --num-threads $threads \
#             --scale-factor 1
#     done
# done
# cd ..

# cd ae-tpce-2pl
# # make dbtest -j
# # echo "------ Make Done ------"
# echo "tpc-e figure8(b) 2PL"
# for threads in {1,2,4,8,12,16,32,48}
# do
#     for((i=1;i<=$count;i++));
#     do
#         theta=3.0
#         echo "tpc-e. cc=2PL. num-threads=$threads, zipf_theta=$theta"
#         ./out-perf.masstree/benchmarks/dbtest \
#             --bench tpce \
#             --parallel-loading \
#             --retry-aborted-transactions \
#             --bench-opt "-w 0,0,0,0,0,0,50,0,0,50 -m 0 -s 1 -a $theta" \
#             --db-type ndb-proto2 \
#             --backoff-aborted-transactions \
#             --runtime $runtime \
#             --num-threads $threads \
#             --scale-factor 1
#     done
# done
# cd ..

echo "------ Figure 8(b) Evaluation Finish ------"

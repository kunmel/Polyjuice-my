package transaction

import (
	"Polyjuice-ethPart/config"
	"Polyjuice-ethPart/policyAnalyzer"
	"Polyjuice-ethPart/utils"
	"bufio"
	"context"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

var TimeCh map[int]chan int

func init() {
	TimeCh = make(map[int]chan int)
	for i := 1; i <= config.THREAD_COUNT; i++ {
		TimeCh[i] = make(chan int)
	}
}

func NewThread(benchMarkType string, threadId int, txnBufSize int, decreaseBackoff [][]int, increaseBackoff [][]int, actionMap [][]int) {
	//policy := readPolicy()
	txFilePath := config.TXFILE_PATH + benchMarkType + "Tx-" + strconv.Itoa(threadId) + ".txt"
	txFile, err := os.Open(txFilePath)
	if err != nil {
		utils.SimplyError("MakeNewThread-open txfile failed: ", err)
	}
	txScanner := bufio.NewScanner(txFile)
	var recordLine string
	ctx, cancel := context.WithTimeout(context.Background(), time.Second*config.RunTime)
	go func() {
		for i := 0; i < config.TXFILE_SIZE; i++ {
			txScanner.Scan()
			recordLine = txScanner.Text()
			record := strings.Split(recordLine, " ")
			txType, _ := strconv.Atoi(record[0])
			execMicroTx(record[1:], threadId, actionMap, txnBufSize, decreaseBackoff, increaseBackoff, txType)
		}
	}()
	<-ctx.Done()
	cancel()
	fmt.Println("++++++++++++" + strconv.Itoa(threadId) + " thread killed++++++++++++")
	return
}

func MakeAllThreads(benchMarkType string, threadNum int, policyPath string) time.Duration {
	startTime := time.Now()
	txnBufSize, decreaseBackoff, increaseBackoff, actionMap, _ := policyAnalyzer.ReadPolicy(policyPath)
	utils.ShowStepInfo("Read & analyze policy completed")
	var wg sync.WaitGroup
	wg.Add(threadNum)
	for i := 1; i <= threadNum; i++ {
		go func(benchMark string, threadId int) {
			NewThread(benchMark, threadId, txnBufSize, decreaseBackoff, increaseBackoff, actionMap)
			wg.Done()
		}(benchMarkType, i)
	}
	wg.Wait()
	utils.ShowStepInfo("All worker threads shut down")
	//time.Sleep(config.RunTime * time.Second)
	//for i := 1; i <= config.THREAD_COUNT; i++ {
	//	TimeCh[i] <- 1
	//}
	execTime := time.Since(startTime)
	return execTime
}

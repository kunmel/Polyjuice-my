package transaction

import (
	"Polyjuice-ethPart/config"
	"Polyjuice-ethPart/ethConnecter"
	"Polyjuice-ethPart/utils"
	"context"
	"fmt"
	"strconv"
	"strings"
	"time"
)

type TxInfo struct {
	TxId   int
	TxType int
}

type LocalInfo struct {
	LocalBuf     [][]string // [key, value, opt] // 如果是read，再在最后加一个"C" or "D"
	LocalRset    [][]string // [key, value, version]
	LocalWset    [][]string // [key, value]
	LocalRMode   []string
	RKeyMap      map[string]int
	WKeyMap      map[string]int
	RSetBack     [][]string
	WSetBack     [][]string
	RKeyMapBack  map[string]int
	WKeyMapBack  map[string]int
	AccessIdBack int
	TXID         int
	Deps         []int
	AbortTimes   int
	AccessListWriteCount int
}

// var doneCh chan int
var doneChs map[int]chan int

func init() {
	// doneCh = make(chan int, 1)
	doneChs = make(map[int]chan int)
	for i := 0; i < config.THREAD_COUNT; i++ {
		doneChs[i+1] = make(chan int, 10000)
	}
}

func ExecuterInit() {
	doneChs = make(map[int]chan int)
	for i := 0; i < config.THREAD_COUNT; i++ {
		doneChs[i+1] = make(chan int, 10000)
	}
}

func getInitInfo() LocalInfo {
	info := LocalInfo{}
	info.RKeyMap, info.WKeyMap, info.RKeyMapBack, info.WKeyMapBack = make(map[string]int), make(map[string]int), make(map[string]int), make(map[string]int)
	info.LocalBuf, info.LocalRset, info.LocalWset, info.RSetBack, info.WSetBack = [][]string{}, [][]string{}, [][]string{}, [][]string{}, [][]string{}
	info.LocalRMode = []string{}
	info.Deps = []int{}
	info.TXID = utils.GetTXID()
	info.AccessIdBack = 1
	info.AbortTimes = 0
	return info
}

func execMicroTx(recordStrs []string, threadId int, actionMap [][]int, txnBufSize int, decreaseBackoff [][]int, increaseBackoff [][]int, txType int) {
	// update(e.g. get(RecordA) & put(RecordA)) * 8
	info := getInitInfo()
	//deps := []string{}
	txIDstr := strconv.Itoa(info.TXID)
	utils.ShowStepInfo("tx " + txIDstr + " start")
	var getResult, putResult, commitResult bool
	i := 1
	for {
		if i > 8 {
			break
		}
		if i%2 == 1 {
			info, getResult, _ = get(recordStrs[i-1], info, threadId, actionMap, txType, i)
			if !getResult {
				DeleteAccesses(info, i, txType)
				utils.UpdateAbortCount()
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " aborted ")
				i = info.AccessIdBack - 1
				fmt.Println(strconv.Itoa(info.TXID) + " get failed, i become " + strconv.Itoa(i))
				info.AbortTimes++
				UpdateBackOff("ABORT", info.AbortTimes, txType, decreaseBackoff, increaseBackoff)
				WaitBackOff(txType)
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " start again")
				utils.WriteLog(txIDstr + "\t" + strconv.Itoa(threadId) + "\tabort\n")
			}
		} else {
			info, putResult, _ = put(recordStrs[i-1], info, threadId, actionMap, txType, i)
			if !putResult {
				DeleteAccesses(info, i, txType)
				utils.UpdateAbortCount()
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " aborted")
				i = info.AccessIdBack - 1
				fmt.Println(strconv.Itoa(info.TXID) + " put failed, i become " + strconv.Itoa(i))
				info.AbortTimes++
				UpdateBackOff("ABORT", info.AbortTimes, txType, decreaseBackoff, increaseBackoff)
				WaitBackOff(txType)
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " start again")
				utils.WriteLog(txIDstr + "\t" + strconv.Itoa(threadId) + "\tabort\n")
			}
		}
		if i == 8 {
			commitResult, _ = commit(info, threadId)
			if !commitResult {
				DeleteAccesses(info, i, txType)
				utils.UpdateAbortCount()
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " aborted")
				i = info.AccessIdBack - 1
				info.AbortTimes++
				UpdateBackOff("ABORT", info.AbortTimes, txType, decreaseBackoff, increaseBackoff)
				WaitBackOff(txType)
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " start again")
				utils.WriteLog(txIDstr + "\t" + strconv.Itoa(threadId) + "\tabort\n")
			} else {
				DelectCommittedTx(info)
				utils.UpdateFinishCount()
				utils.ShowStepInfo("tx " + txIDstr + " of thread " + strconv.Itoa(threadId) + " committed")
				info.AbortTimes = 0
				UpdateBackOff("COMMIT", info.AbortTimes, txType, decreaseBackoff, increaseBackoff)
				utils.WriteLog(txIDstr + "\t" + strconv.Itoa(threadId) + "\tcommit\n")
			}
		}
		i++
		// time.Sleep(2 * time.Second)
	}
}

//func getRoutinue(recordStrs []string, info LocalInfo, threadId int, actionMap [][]int, txType int, i int) (LocalInfo, bool){
//	var getResult bool
//	ctx, cancel := context.WithTimeout(context.Background(), time.Second*15)
//	go func(record string) {
//		info, getResult, _ = get(record, info, threadId, actionMap, txType, i)
//		doneChs[threadId] <- 1
//	}(recordStrs[i-1])
//	select {
//	case <-ctx.Done():
//		getResult = false
//		fmt.Println(strconv.Itoa(info.TXID) + "abort for timeout")
//		cancel()
//	case <-doneChs[threadId]:
//		cancel()
//	}
//	return info, getResult
//}
//
//func putRoutinue(recordStrs []string, info LocalInfo, threadId int, actionMap [][]int, txType int, i int) (LocalInfo, bool){
//	var putResult bool
//	ctx, cancel := context.WithTimeout(context.Background(), time.Second*15)
//	go func(record string) {
//		info, putResult, _ = put(record, info, threadId, actionMap, txType, i)
//		doneChs[threadId] <- 1
//	}(recordStrs[i-1])
//	select {
//	case <-ctx.Done():c
//		putResult = false
//		fmt.Println(strconv.Itoa(info.TXID) + "abort for timeout")
//		cancel()
//	case <-doneChs[threadId]:
//		cancel()
//	}
//	return info, putResult
//}
//
//func commitRoutinue(reocrdStrs []string, info LocalInfo, threadId int, actionMap [][]int, txType int, i int) bool{
//	var commitResult bool
//	ctx, cancel := context.WithTimeout(context.Background(), time.Second*15)
//	go func() {
//		commitResult, _ = commit(info, threadId)
//		doneChs[threadId] <- 1
//	}()
//	select {
//	case <-ctx.Done():
//		commitResult = false
//		fmt.Println(strconv.Itoa(info.TXID) + "abort for timeout")
//		cancel()
//	case <-doneChs[threadId]:
//		cancel()
//	}
//	return commitResult
//}

func get(recordKey string, info LocalInfo, threadId int, actionMap [][]int, txType int, accessId int) (LocalInfo, bool, int) {
	actionMapAccessId := (txType-1)*config.MICRO_ACCESSEACH + accessId - 1
	ctx, cancel := context.WithTimeout(context.Background(), time.Second * config.WaitTimeOut)
	waitTimeOut := wait(info, actionMap, threadId, txType, actionMapAccessId, ctx)
	cancel()
	if !waitTimeOut {
		return info, false, info.AccessIdBack
	}
	fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "get-wait-1 done")
	var value, version string
	var existDirty bool
	// dirty Read
	if actionMap[actionMapAccessId][0] == 1 {
		value, version, existDirty = FindLastWrite(recordKey)
		if !existDirty {
			value, version = ethConnecter.QueryETH(recordKey)
			fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "get-queryEth-2(dirty read) done")
			info.LocalRMode = append(info.LocalRMode, "D")
			info.LocalBuf = append(info.LocalBuf, []string{recordKey, value, "READ", "D"})
		} else {
			info.LocalRMode = append(info.LocalRMode, "C")
			info.LocalBuf = append(info.LocalBuf, []string{recordKey, value, "READ", "C"})
		}
	} else { // clean Read
		value, version = ethConnecter.QueryETH(recordKey)
		info.LocalRMode = append(info.LocalRMode, "C")
		info.LocalBuf = append(info.LocalBuf, []string{recordKey, value, "READ", "C"})
		fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "get-CleanRead-2 done")
	}
	//info.LocalBuf = append(info.LocalBuf, []string{recordKey, value, "READ"})
	info.LocalRset = append(info.LocalRset, []string{recordKey, value, version})
	if actionMap[actionMapAccessId][2] == 1 {
		fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + " try early validate")
		if !early_validate(info, threadId) {
			info.LocalRset = info.RSetBack
			info.RKeyMap = info.RKeyMapBack
			info.LocalWset = info.WSetBack
			info.WKeyMap = info.WKeyMapBack
			info.LocalBuf = [][]string{}
			fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "early validate fail")
			return info, false, info.AccessIdBack
		} else {
			info.RSetBack = info.LocalRset
			info.RKeyMapBack = info.RKeyMap
			info.WSetBack = info.LocalWset
			info.WKeyMapBack = info.WKeyMap
			info.AccessIdBack = accessId
			info, _ = WriteAccessList(info, txType, accessId)
			fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + " try early validate success")
		}
		info.LocalBuf = [][]string{}
	}
	fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + " get done")
	return info, true, 1
}

func put(recordKey string, info LocalInfo, threadId int, actionMap [][]int, txType int, accessId int) (LocalInfo, bool, int) {
	actionMapAccessId := (txType-1)*config.MICRO_ACCESSEACH + accessId - 1
	dummyValue := "dummyValue"
	ctx, cancel := context.WithTimeout(context.Background(), time.Second * config.WaitTimeOut)
	waitTimeOut := wait(info, actionMap, threadId, txType, actionMapAccessId, ctx)
	cancel()
	if !waitTimeOut {
		return info, false, info.AccessIdBack
	}
	fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "put-wait-1 done")
	info.LocalBuf = append(info.LocalBuf, []string{recordKey, dummyValue, "WRITE"})
	info.LocalWset = append(info.LocalWset, []string{recordKey, dummyValue})
	info.WKeyMap[recordKey] = 1
	if actionMap[actionMapAccessId][2] == 1 {
		fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + " try early validate")
		if !early_validate(info, threadId) {
			info.LocalRset = info.RSetBack
			info.RKeyMap = info.RKeyMapBack
			info.LocalWset = info.WSetBack
			info.WKeyMap = info.WKeyMapBack
			info.LocalBuf = [][]string{}
			fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "early validate fail")
			return info, false, info.AccessIdBack
		} else {
			info.RSetBack = info.LocalRset
			info.RKeyMapBack = info.RKeyMap
			info.WSetBack = info.LocalWset
			info.WKeyMapBack = info.WKeyMap
			info.AccessIdBack = accessId
			info, _ = WriteAccessList(info, txType, accessId)
			fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "early validate success")
		}
		info.LocalBuf = [][]string{}
	}
	fmt.Println(strconv.Itoa(info.TXID) + "-" + strconv.Itoa(accessId) + "put done")
	return info, true, 1
}

func commit(info LocalInfo, threadId int) (bool, int) {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second * config.WaitTimeOut)
	timeout := waitDeps(info, threadId, ctx)
	cancel()
	if !timeout {
		return false, 1
	}
	fmt.Println(strconv.Itoa(info.TXID) + "commit-waitdeps done")
	if validate(info.LocalRset) {
		writeToEth(info)
	} else {
		fmt.Println(strconv.Itoa(info.TXID) + " abort for check version fail in commit")
		return false, 0
	}
	return true, 1
}

func early_validate(info LocalInfo, threadId int) bool {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second * config.WaitTimeOut)
	timeout := waitDeps(info, threadId, ctx)
	cancel()
	if !timeout {
		return false
	}
	return validate(info.LocalRset)
}

func validate(rset [][]string) bool {
	if len(rset) == 0 {
		return true
	}
	return checkVersion(rset)
}

// 先read accessList确认前面有哪些tx以及这些tx当前的access，然后监听channel，直到所有需要wait的tx执行到对应的access。
// waitPolicy, key为txType， value为accessId
func wait(info LocalInfo, actionMap [][]int, threadId int, txType int, actionMapAccessId int, ctx context.Context) bool {
	fmt.Println(strconv.Itoa(info.TXID)+" actionMapAccessId: ", actionMapAccessId)
	waitPolicy := map[int]int{}
	for i := 3; i < config.MICRO_ACTION_NUM; i++ {
		waitPolicy[i-2] = actionMap[actionMapAccessId][i]
	}
	fmt.Println(strconv.Itoa(info.TXID)+" waitPolicy: ", waitPolicy)
	// 查accessList跟wait policy，确定需要wait的map，把当前access list中所有的需要wait的筛选出来
	// key为txId, value为[txType, accessId]
	curWaitMap := GetCurrentWaitMap(waitPolicy, info.TXID)
	fmt.Println(strconv.Itoa(info.TXID)+" get current curWaitMap: ", curWaitMap)
	// 等待ch消息 or Timeout
	forFlag := true
	for {
		if len(curWaitMap) == 0 {
			break
		}
		select {
		case <- ctx.Done():
			forFlag = false
		case newWrite := <-ALChannels[threadId]:
			fmt.Println(newWrite)
			if strings.ToUpper(newWrite.Opt) == "ALLABORT" {
				delete(curWaitMap, newWrite.TxId)
				continue
			}
			// 如果是一个相同的tx，不需要监听
			if newWrite.TxId == info.TXID {
				continue
			}
			// 如果需要监听，但没还没监听，即是新的事务
			if _, ok := curWaitMap[newWrite.TxId]; !ok {
				curWaitMap[newWrite.TxId] = []int{newWrite.TxType, waitPolicy[newWrite.TxType]}
				fmt.Println(strconv.Itoa(info.TXID)+" add new tx to curWaitMap: ", curWaitMap)
			}
			// 如果这个已经完成了
			if newWrite.TxAccessId >= waitPolicy[newWrite.TxType] {
				delete(curWaitMap, newWrite.TxId)
				fmt.Println(strconv.Itoa(info.TXID)+" after delete new tx from curWaitMap: ", curWaitMap)
				if len(curWaitMap) == 0 {
					break
				}
			}
		}
		if !forFlag {
			break
		}
	}
	return forFlag
}

func waitDeps(info LocalInfo, threadId int, ctx context.Context) bool {
	forFlag := true
	// 确认目前还有哪些依赖的tx没有提交
	deps := GetNewDeps(info)
	// 等待channel消息，全部提交or超时
	for {
		if len(deps) == 0 {
			break
		}
		select {
		case <-ctx.Done():
			forFlag = false
			break
		case newCommit := <-ALChannels[threadId]:
			if strings.ToUpper(newCommit.Opt) != "COMMIT" {
				continue
			}
			if _, ok := deps[newCommit.TxId]; ok {
				delete(deps, newCommit.TxId)
			}
		}
		if !forFlag {
			break
		}
	}
	return forFlag
}

func writeToEth(info LocalInfo) {
	for _, set := range info.LocalRset {
		_ = ethConnecter.WriteETH(set[0], set[1], set[2])
	}
}

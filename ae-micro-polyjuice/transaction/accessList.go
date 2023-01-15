package transaction

import (
	"Polyjuice-ethPart/config"
	"strconv"
	"strings"
	"sync"
)

type AccessListMsg struct {
	TxId       int
	TxType     int
	TxAccessId int
	Opt        string
}

type ACCESSLIST_ELEMENT struct {
	TxId       int
	TxType     int
	TxAccessId int    // 是这个TX的第几个access
	Operation  string // R OR W
	Key        string
	Version    string
	Value      string
}

var (
	ALChannels map[int]chan AccessListMsg
	lock       sync.RWMutex
	AccessList []ACCESSLIST_ELEMENT
)

func init() {
	ALChannels = make(map[int]chan AccessListMsg)
	for i := 0; i < config.THREAD_COUNT; i++ {
		ALChannels[i+1] = make(chan AccessListMsg, 10000)
	}
}

func AccessListInit() {
	ALChannels = make(map[int]chan AccessListMsg)
	for i := 0; i < config.THREAD_COUNT; i++ {
		ALChannels[i+1] = make(chan AccessListMsg, 10000)
	}
	AccessList = []ACCESSLIST_ELEMENT{}
}

func ReadAccessList() {
	lock.RLock()

	lock.RUnlock()
}

func GetCurrentWaitMap(waitPolicy map[int]int, curTxId int) map[int][]int {
	waitMap := make(map[int][]int)
	lock.RLock()
	for _, element := range AccessList {
		if _, ok := waitPolicy[element.TxType]; !ok {
			continue
		} else if element.TxId != curTxId {
			_, OK := waitMap[element.TxId]
			if !OK && element.TxAccessId < waitPolicy[element.TxType] {
				waitMap[element.TxId] = []int{element.TxType, waitPolicy[element.TxType]}
			} else if OK && element.TxAccessId < waitPolicy[element.TxType] {
				waitMap[element.TxId] = []int{element.TxType, waitPolicy[element.TxType]}
			} else if OK && element.TxAccessId >= waitPolicy[element.TxType] {
				delete(waitMap, element.TxId)
			}
		}
	}
	lock.RUnlock()
	return waitMap
}

// 此处accessID为对当前txtype的第几个access(从1开始)
//func WriteAccessList(txInfo TxInfo, accessId int, opt []string, keys []string, versions []string, values []string) error {
//	err :=  errors.New("len(keys) != len(values)")
//	if len(keys) != len(values) {
//		utils.SimplyError("WriteAccessList", err)
//		return err
//	}
//	lock.Lock()
//	for idx:=0; idx<len(keys); idx++ {
//		element := ACCESSLIST_ELEMENT{
//			TxId:       txInfo.TxId,
//			TxType:     txInfo.TxType,
//			TxAccessId: accessId,
//			Operation:  opt[idx],
//			Key:        keys[idx],
//			Version:    versions[idx],
//			Value:      values[idx],
//		}
//		AccessList = append(AccessList, element)
//	}
//	// 写一个channel用来发送当前要写accessList的tx以及access，让其他正在wait的thread知道什么时候wait结束。
//	writeMsg := AccessListMsg{txInfo.TxId, txInfo.TxType, accessId, "WRITE"}
//	for _, ch := range ALChannels {
//		ch <- writeMsg
//	}
//	lock.Unlock()
//	return nil
//}

func WriteAccessList(info LocalInfo, txType int, accessId int) (LocalInfo, error) {
	lock.Lock()
	for idx := 0; idx < len(info.LocalRset); idx++ {
		element := ACCESSLIST_ELEMENT{
			TxId:       info.TXID,
			TxType:     txType,
			TxAccessId: accessId,
			Operation:  "R",
			Key:        info.LocalRset[idx][0],
			Version:    info.LocalRset[idx][2],
			Value:      info.LocalRset[idx][1],
		}
		if info.LocalRMode[idx] == "C" {
			AccessList = append([]ACCESSLIST_ELEMENT{element}, AccessList...)
			info.AccessListWriteCount++
		} else {
			AccessList = append(AccessList, element)
			info.AccessListWriteCount++
		}
	}
	for idx := 0; idx < len(info.LocalWset); idx++ {
		element := ACCESSLIST_ELEMENT{
			TxId:       info.TXID,
			TxType:     txType,
			TxAccessId: accessId,
			Operation:  "W",
			Key:        info.LocalWset[idx][0],
			Version:    strconv.Itoa(info.TXID),
			Value:      info.LocalWset[idx][1],
		}
		AccessList = append(AccessList, element)
		info.AccessListWriteCount++
	}
	// 写一个channel用来发送当前要写accessList的tx以及access，让其他正在wait的thread知道什么时候wait结束。
	writeMsg := AccessListMsg{info.TXID, txType, accessId, "WRITE"}
	for _, ch := range ALChannels {
		ch <- writeMsg
	}
	lock.Unlock()
	return info, nil
}

func GetNewDeps(txInfo LocalInfo) map[int]int {
	newDeps := make(map[int]int)
	w := make(map[string]int)
	r := make(map[string]int)
	lock.RLock()
	// 先把还没到access list里的键处理，如果写直接插入，如果是dirty read也插入，如果是clean read不用，因为他会被插入到accessList的最前面
	for i := 0; i < len(txInfo.LocalBuf); i++ {
		if txInfo.LocalBuf[i][2] == "WRITE" {
			w[txInfo.LocalBuf[i][0]] = 1
		} else {
			if txInfo.LocalBuf[i][3] == "D" {
				r[txInfo.LocalBuf[i][0]] = 1
			}
		}
	}
	// 从后向前遍历access list，
	for i := len(AccessList) - 1; i >= 0; i-- {
		if AccessList[i].TxId == txInfo.TXID {
			if strings.ToUpper(AccessList[i].Operation) == "W" {
				w[AccessList[i].Key] = 1
			} else {
				r[AccessList[i].Key] = 1
			}
			continue
		}
		if w[AccessList[i].Key] == 1 {
			newDeps[AccessList[i].TxId] = 1
		} else if r[AccessList[i].Key] == 1 {
			if strings.ToUpper(AccessList[i].Operation) == "W" {
				newDeps[AccessList[i].TxId] = 1
			}
		}
	}
	lock.RUnlock()
	return newDeps
}

func DelectCommittedTx(txInfo LocalInfo) {
	lock.Lock()
	newList := []ACCESSLIST_ELEMENT{}
	for i := 0; i < len(AccessList); i++ {
		if AccessList[i].TxId == txInfo.TXID {
			continue
		} else {
			element := AccessList[i]
			newList = append(newList, element)
		}
	}
	AccessList = newList
	lock.Unlock()
}

func DeleteAccesses(txInfo LocalInfo, curAccessID int, txType int) {
	lock.Lock()
	newList := []ACCESSLIST_ELEMENT{}
	// aborted := false
	if txInfo.AccessIdBack == 1 {
		writeMsg := AccessListMsg{txInfo.TXID, txType, 1, "ALLABORT"}
		for _, ch := range ALChannels {
			ch <- writeMsg
		}
		for i := 0; i < len(AccessList); i++ {
			if AccessList[i].TxId == txInfo.TXID{
				continue
			} else {
				element := AccessList[i]
				newList = append(newList, element)
			}
		}
	} else {
		for i := 0; i < len(AccessList); i++ {
			if AccessList[i].TxId == txInfo.TXID && AccessList[i].TxAccessId >= txInfo.AccessIdBack {
				continue
			} else {
				element := AccessList[i]
				newList = append(newList, element)
			}
		}
	}
	AccessList = newList
	lock.Unlock()
}

func FindLastWrite(key string) (string, string, bool) {
	for i := len(AccessList) - 1; i >= 0; i-- {
		if AccessList[i].Key == key && strings.ToUpper(AccessList[i].Operation) == "W" {
			return AccessList[i].Value, AccessList[i].Version, true
		}
	}
	return "", "", false
}

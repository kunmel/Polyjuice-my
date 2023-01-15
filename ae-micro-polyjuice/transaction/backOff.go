package transaction

import (
	"Polyjuice-ethPart/config"
	"fmt"
	"strconv"
	"strings"
	"sync"
	"time"
)

var BackOffMap map[int]float64 // map[txType] time
var backOffLock sync.RWMutex

func init() {
	BackOffMap = make(map[int]float64)
	for i := 1; i <= config.MICRO_TXTYPECOUNT; i++ {
		BackOffMap[i] = 0.5
	}
}
func BackOffInit() {
	BackOffMap = make(map[int]float64)
	for i := 1; i <= config.MICRO_TXTYPECOUNT; i++ {
		BackOffMap[i] = 0.5
	}
}

func UpdateBackOff(resultType string, abortTimes int, txType int, decreaseBackoff [][]int, increaseBackoff [][]int) {
	backOffLock.Lock()
	if abortTimes >= 2 {
		abortTimes = 2
	}
	if strings.ToUpper(resultType) == "COMMIT" {
		alpha := decreaseBackoff[abortTimes][txType-1]
		BackOffMap[txType] = BackOffMap[txType] / float64(1+alpha)
	} else {
		alpha := increaseBackoff[abortTimes][txType-1]
		BackOffMap[txType] = BackOffMap[txType] * float64(1+alpha)
	}
	backOffLock.Unlock()
}

func WaitBackOff(txType int) {
	var backOffTime float64
	backOffLock.RLock()
	backOffTime = BackOffMap[txType]
	backOffLock.RUnlock()
	fmt.Println("type " + strconv.Itoa(txType)+"new backOffTime is ", backOffTime)
	time.Sleep(time.Duration(backOffTime) * time.Second)
}

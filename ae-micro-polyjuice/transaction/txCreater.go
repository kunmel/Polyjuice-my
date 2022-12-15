package transaction

import (
	"Polyjuice-env/config"
	"Polyjuice-env/utils"
	"fmt"
	"math/rand"
	"os"
	"strconv"
)

const K = 1000
const M = 1000000

var seed = 0

func CreateMicroTx(zipfArray []float64, randRecordCount int, accessEach int, txType int, seed int64) string {
	//rand.Seed(seed)
	txStr := ""
	// 先插入txType
	txStr += strconv.Itoa(txType) + " "
	// 先生成每个TX的前2个access，在zipf中选择一个record，此部分目前最多预留5k个
	zipfRand := utils.GetZipfRand(2, zipfArray, seed)
	txStr += strconv.Itoa(zipfRand[0]) + " "
	txStr += strconv.Itoa(zipfRand[1]) + " "
	// 生成除前2个、最后2个的access，随机选择。当前设置为5k~5M
	for i := 0; i < accessEach-4; i++ {
		txStr += strconv.Itoa(rand.Intn(randRecordCount-5*K)+5*K) + " "
	}
	// 生成最后2个，每个txType都不同，每种2k个中随机选
	txStr += strconv.Itoa(txType) + "-" + strconv.Itoa(rand.Intn(1*K)) + " "
	txStr += strconv.Itoa(txType) + "-" + strconv.Itoa(rand.Intn(1*K))
	txStr += "\n"
	return txStr
}

func MicroTxFileBuilder(totalCount int, threadNum int) {
	txFilePath := "./transaction/txFile/"
	txFileName := "microTx-" + strconv.Itoa(threadNum) + ".txt"
	zipfTheta := 0.2
	zipfRecordCount := 1 * K
	randRecordCount := 50 * K
	file, err := os.Create(txFilePath + txFileName)
	if err != nil {
		utils.SimplyError("MicroTxFileBuilder-open txFile", err)
		return
	}
	zipfArray := utils.MakeZipfArray(zipfTheta, zipfRecordCount)
	for i := 0; i < config.TXFILE_SIZE; i++ {
		curType := rand.Intn(config.MICRO_TXTYPECOUNT) + 1
		seed++
		fmt.Println(seed)
		tx := CreateMicroTx(zipfArray, randRecordCount, config.MICRO_ACCESSEACH, curType, int64(seed))
		_, err := file.WriteString(tx)
		if err != nil {
			utils.SimplyError("MicroTxFileBuilder-write txFile", err)
			return
		}
	}
}

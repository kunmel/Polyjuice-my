package transaction

import (
	"Polyjuice-ethPart/config"
	"Polyjuice-ethPart/ethConnecter"
	"log"
	"strconv"
	"sync"
)

var versionListLock sync.RWMutex
var versionList map[string]int

func init() {
	versionList = make(map[string]int)

	for j := 0; j <= 200; j++ {
		keys := []string{}
		for n := j * 250; n < (j+1)*250; n++ {
			newKey := strconv.Itoa(n)
			keys = append(keys, newKey)
		}
		_, versions := ethConnecter.BatchQueryETH(keys)
		for i := 0; i < len(versions); i++ {
			version, err := strconv.Atoi(versions[i])
			if err != nil {
				log.Fatal("query from eth, version is not a int")
			}
			versionList[keys[i]] = version
		}
		//fmt.Println(strconv.Itoa(j*200) + "~" + strconv.Itoa(j*200+200) + "query done")
	}

	for i := 1; i <= config.MICRO_TXTYPECOUNT; i++ {
		key := strconv.Itoa(i) + "-"
		for j := 0; j <= 10; j++ {
			keys := []string{}
			for n := j * 100; n < (j+1)*100; n++ {
				keys = append(keys, key+strconv.Itoa(n))
			}
			_, versions := ethConnecter.BatchQueryETH(keys)
			for i := 0; i < len(versions); i++ {
				version, err := strconv.Atoi(versions[i])
				if err != nil {
					log.Fatal("query from eth, version is not a int")
				}
				versionList[keys[i]] = version
			}
			//fmt.Println(key + strconv.Itoa(j*100) + "~" + strconv.Itoa(j*100+100) + "query done")
		}
	}
}

func VersionListInit() {
	versionList = make(map[string]int)

	for j := 0; j <= 200; j++ {
		keys := []string{}
		for n := j * 250; n < (j+1)*250; n++ {
			newKey := strconv.Itoa(n)
			keys = append(keys, newKey)
		}
		_, versions := ethConnecter.BatchQueryETH(keys)
		for i := 0; i < len(versions); i++ {
			version, err := strconv.Atoi(versions[i])
			if err != nil {
				log.Fatal("query from eth, version is not a int")
			}
			versionList[keys[i]] = version
		}
		//fmt.Println(strconv.Itoa(j*200) + "~" + strconv.Itoa(j*200+200) + "query done")
	}

	for i := 1; i <= config.MICRO_TXTYPECOUNT; i++ {
		key := strconv.Itoa(i) + "-"
		for j := 0; j <= 10; j++ {
			keys := []string{}
			for n := j * 100; n < (j+1)*100; n++ {
				keys = append(keys, key+strconv.Itoa(n))
			}
			_, versions := ethConnecter.BatchQueryETH(keys)
			for i := 0; i < len(versions); i++ {
				version, err := strconv.Atoi(versions[i])
				if err != nil {
					log.Fatal("query from eth, version is not a int")
				}
				versionList[keys[i]] = version
			}
			//fmt.Println(key + strconv.Itoa(j*100) + "~" + strconv.Itoa(j*100+100) + "query done")
		}
	}
}

func updateVersion(key string, version int) {
	versionListLock.Lock()
	versionList[key] = version
	versionListLock.Unlock()
}

func getVersion(key string) int {
	version := 0
	versionListLock.RLock()
	version = versionList[key]
	versionListLock.RUnlock()
	return version
}

func checkVersion(rset [][]string) bool {
	//fmt.Println(rset)
	versionListLock.RLock()
	for _, kvv := range rset {
		newestVersion := getVersion(kvv[0])
		oldVersion, err := strconv.Atoi(kvv[2])
		if err != nil || newestVersion > oldVersion {
			return false
		} else {
			continue
		}
	}
	versionListLock.RUnlock()
	return true
}

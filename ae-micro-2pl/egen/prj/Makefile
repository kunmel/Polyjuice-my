#
# Makefile to build EGenLoader, EGenValidate and EGen driver support code.
#

# PORTABILITY NOTES
# EGen makes use of "standardized" printf/scanf format specifiers
# and constant declaration macros which are typically defined in
# <inttypes.h> on Un*x-like platforms.  In order to get EGen to
# compile on some platforms, it may be neccessary to set one (or
# both) of -D__STDC_FORMAT_MACROS and -D__STDC_CONSTANT_MACROS.
#
# Defines for HP-UX
# CXX=/opt/aCC/bin/aCC
# CCFLAGS=+DD64 -Aa -O -ext -mt
# LDFLAGS=+DD64 -Aa -O -ext -mt
# AR=ar

# Platform specific defines
#CXX=
#CCFLAGS=
#LDFLAGS=
#AR=
#ARFLAGS=

# Application specific defines
APPDEFINES=-DCOMPILE_FLAT_FILE_LOAD

# Directory for source files
SRC = ../src
# Directory for project files
PRJ = ../prj
# Directory for intermediate files
OBJ = ../obj
# Directory for include files
INC = ../inc
# Directory for library files
LIB = ../lib
# Directory for executable files
EXE = ../bin

EGEN_UTILITIES_SRC = ../Utilities/src
EGEN_INPUTFILES_SRC = ../InputFiles/src

VPATH=$(SRC):$(EGEN_UTILITIES_SRC):$(EGEN_INPUTFILES_SRC):$(OBJ)

.SUFFIXES: .cpp
.cpp.o:
	$(CXX) $(CCFLAGS) $(APPDEFINES) -c $< -o $(OBJ)/$@

# Specify each library as a single target
# with source and include files separates into different variables.

EGenDriverLib_lib =		libegen.a

EGenDriverCELib_src =		CE.cpp \
				CETxnMixGenerator.cpp \
				CETxnInputGenerator.cpp

EGenDriverCELib_obj =		$(EGenDriverCELib_src:.cpp=.o)

EGenDriverDMLib_src =		DM.cpp

EGenDriverDMLib_obj =		$(EGenDriverDMLib_src:.cpp=.o)


EGenDriverMEELib_src =		MEE.cpp \
				MEEPriceBoard.cpp \
				MEESecurity.cpp \
				MEETickerTape.cpp \
				MEETradingFloor.cpp \
				WheelTime.cpp

EGenDriverMEELib_obj =		$(EGenDriverMEELib_src:.cpp=.o)


EGenLogger_src =		BaseLogger.cpp EGenLogFormatterTab.cpp

EGenLogger_obj =		$(EGenLogger_src:.cpp=.o)


EGenTables_src =		AddressTable.cpp \
				ChargeTable.cpp \
				CommissionRateTable.cpp \
				CustomerSelection.cpp \
				CustomerTable.cpp \
				ExchangeTable.cpp \
				IndustryTable.cpp \
				Person.cpp \
				SectorTable.cpp \
				StatusTypeTable.cpp \
				TaxRateTable.cpp \
				TradeGen.cpp \
				TradeTypeTable.cpp \
				ZipCodeTable.cpp

EGenTables_obj =		$(EGenTables_src:.cpp=.o)


EGenUtilities_src =		DateTime.cpp \
				error.cpp \
				Random.cpp \
				Money.cpp \
				EGenVersion.cpp \
				locking.cpp \
				threading.cpp \
				condition.cpp

EGenUtilities_obj =		$(EGenUtilities_src:.cpp=.o)

EGenUtilities_inc =		EGenUtilities_stdafx.h \
				DateTime.h \
				FixedArray.h \
				FixedMap.h \
				Random.h \
				TableConsts.h \
				error.h


EGenInputFiles_src =		AreaCodeDataFileRecord.cpp \
				ChargeDataFileRecord.cpp \
				CommissionRateDataFileRecord.cpp \
				CompanyCompetitorDataFileRecord.cpp \
				CompanyCompetitorFile.cpp \
				CompanyDataFileRecord.cpp \
				CompanyFile.cpp \
				CompanySPRateDataFileRecord.cpp \
				DataFileManager.cpp \
				ExchangeDataFileRecord.cpp \
				FemaleFirstNameDataFileRecord.cpp \
				IndustryDataFileRecord.cpp \
				LastNameDataFileRecord.cpp \
				MaleFirstNameDataFileRecord.cpp \
				NewsDataFileRecord.cpp \
				NonTaxableAccountNameDataFileRecord.cpp \
				SectorDataFileRecord.cpp \
				SecurityDataFileRecord.cpp \
				SecurityFile.cpp \
				StatusTypeDataFileRecord.cpp \
				StreamSplitter.cpp \
				StreetNameDataFileRecord.cpp \
				StreetSuffixDataFileRecord.cpp \
				StringSplitter.cpp \
				TaxRateCountryDataFileRecord.cpp \
				TaxRateDivisionDataFileRecord.cpp \
				TaxRateFile.cpp \
				TaxableAccountNameDataFileRecord.cpp \
				TextFileSplitter.cpp \
				TradeTypeDataFileRecord.cpp \
				Utilities.cpp \
				ZipCodeDataFileRecord.cpp

EGenInputFiles_obj =		$(EGenInputFiles_src:.cpp=.o)

EGenInputFiles_inc =		AreaCodeDataFileRecord.h \
				BucketedDataFile.h \
				ChargeDataFileRecord.h \
				CommissionRateDataFileRecord.h \
				CompanyCompetitorDataFileRecord.h \
				CompanyCompetitorFile.h \
				CompanyDataFileRecord.h \
				CompanyFile.h \
				CompanySPRateDataFileRecord.h \
				DataFile.h \
				DataFileManager.h \
				DataFileTypes.h \
				ExchangeDataFileRecord.h \
				FemaleFirstNameDataFileRecord.h \
				ITaxRateFileRecord.h \
				ITextSplitter.h \
				IndustryDataFileRecord.h \
				LastNameDataFileRecord.h \
				MaleFirstNameDataFileRecord.h \
				NewsDataFileRecord.h \
				NonTaxableAccountNameDataFileRecord.h \
				SectorDataFileRecord.h \
				SecurityDataFileRecord.h \
				SecurityFile.h \
				ShrinkToFit.h \
				StatusTypeDataFileRecord.h \
				StreamSplitter.h \
				StreetNameDataFileRecord.h \
				StreetSuffixDataFileRecord.h \
				StringSplitter.h \
				TaxRateCountryDataFileRecord.h \
				TaxRateDivisionDataFileRecord.h \
				TaxRateFile.h \
				TaxableAccountNameDataFileRecord.h \
				TextFileSplitter.h \
				TradeTypeDataFileRecord.h \
				WeightedDataFile.h \
				ZipCodeDataFileRecord.h

FlatFileLoader_src =		FlatFileLoader.cpp

FlatFileLoader_obj =		$(FlatFileLoader_src:.cpp=.o)


EGenGenerateAndLoad_src =	EGenGenerateAndLoad.cpp

EGenGenerateAndLoad_obj =	$(EGenGenerateAndLoad_src:.cpp=.o)


EGenLoader_src =		EGenLoader.cpp

EGenLoader_obj =		$(EGenLoader_src:.cpp=.o)


EGenValidate_src =		EGenValidate.cpp strutil.cpp progressmeter.cpp progressmeterinterface.cpp bucketsimulator.cpp threading.cpp

EGenValidate_obj =		$(EGenValidate_src:.cpp=.o)


# Using pattern rules that were defined earlier. 
# All options are specified through the variables.

all:				EGenDriverLib EGenLoader EGenValidate

EGenLoader:			EGenUtilities \
				EGenInputFiles \
				EGenDriverMEELib \
				EGenLogger \
				EGenTables \
				FlatFileLoaderObj \
				EGenGenerateAndLoadObj \
				$(EGenLoader_obj)
	cd $(OBJ); \
	$(CXX) 	$(LDFLAGS) \
				$(EGenUtilities_obj) \
				$(EGenInputFiles_obj) \
				$(EGenDriverMEELib_obj) \
				$(EGenLogger_obj) \
				$(EGenTables_obj) \
				$(FlatFileLoader_obj) \
				$(EGenGenerateAndLoad_obj) \
				$(EGenLoader_obj) \
				$(LIBS) \
				-o $(EXE)/$@; \
	cd $(PRJ); \
	ls -l $(EXE)

EGenValidate:			EGenDriverLib \
				$(EGenValidate_obj)
	cd $(OBJ); \
	$(CXX)  $(LDFLAGS) \
				$(EGenValidate_obj) \
				$(LIB)/$(EGenDriverLib_lib) \
				$(LIBS) \
				-o $(EXE)/$@; \
	cd $(PRJ); \
	ls -al $(EXE)

EGenDriverLib:			EGenDriverCELib \
				EGenDriverDMLib \
				EGenDriverMEELib \
				EGenUtilities \
				EGenInputFiles \
				EGenLogger \
				EGenTables
	cd $(OBJ); \
	echo $(ARFLAGS); \
	$(AR) $(ARFLAGS) $(LIB)/$(EGenDriverLib_lib) \
				$(EGenDriverCELib_obj) \
				$(EGenDriverDMLib_obj) \
				$(EGenDriverMEELib_obj) \
				$(EGenUtilities_obj) \
				$(EGenInputFiles_obj) \
				$(EGenLogger_obj) \
				$(EGenTables_obj); \
	cd $(PRJ); \
	ls -l  $(LIB)

EGenDriverCELib:		$(EGenDriverCELib_obj)

EGenDriverDMLib:		$(EGenDriverDMLib_obj)

EGenDriverMEELib:		$(EGenDriverMEELib_obj)

EGenLogger:			$(EGenLogger_obj)

EGenUtilities:			$(EGenUtilities_obj)

EGenInputFiles:			$(EGenInputFiles_obj)

EGenTables:			$(EGenTables_obj) 

FlatFileLoaderObj:		$(FlatFileLoader_obj)

EGenGenerateAndLoadObj:		$(EGenGenerateAndLoad_obj)

clean:
	cd $(OBJ); \
	rm -f			$(EGenUtilities_obj) \
				$(EGenInputFiles_obj) \
				$(EGenDriverCELib_obj) \
				$(EGenDriverDMLib_obj) \
				$(EGenDriverMEELib_obj) \
				$(EGenLogger_obj) \
				$(EGenTables_obj) \
				$(FlatFileLoader_obj) \
				$(EGenGenerateAndLoad_obj) \
				$(EGenValidate_obj) \
				$(EGenLoader_obj); \
	cd $(LIB); \
	rm -f			$(EGenDriverLib_lib); \
	cd $(EXE); \
	rm -f			EGenLoader EGenValidate; \
	cd $(PRJ)

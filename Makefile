STDIOWRAP_DIR = src/stdiowrap5
MCW_DIR       = src/mcw5
NCBI_DIR      = src/ncbi_c--May_15_2009_5
TARGET_DIR    = target

.PHONY: stdiowrap mcw ncbi

all: stdiowrap mcw ncbi

stdiowrap:
	$(MAKE) -C $(STDIOWRAP_DIR)

mcw:
	$(MAKE) -C $(MCW_DIR)

ncbi: stdiowrap
	$(MAKE) -C $(NCBI_DIR)

clean:
	$(MAKE) -C $(STDIOWRAP_DIR) clean
	$(MAKE) -C $(MCW_DIR) clean
	$(MAKE) -C $(NCBI_DIR) clean
	rm -rf target

install: all
	mkdir -p $(TARGET_DIR)
	cp -f $(MCW_DIR)/{mcw,sortfasta,SHMclean,zextract,wrap,xmlparse,fastalens,fastadist,compressmol2,splitmol2,splitfasta} $(TARGET_DIR)
	cp -f $(NCBI_DIR)/ncbi/build/blastall $(TARGET_DIR)


#
# RNA settings
#

# jemalloc
export LD_LIBRARY_PATH=/usr/local/lib
export MALLOC_CONF="background_thread:true,metadata_thp:auto,dirty_decay_ms:3000000,muzzy_decay_ms:3000000"
# RNA launch scripts and executable files
export RNA_ROOT=/home/rna/RNA
export RNA_USER=rna
export MONGO_BIN_DIR=/usr/bin
export MONGO_DATA_DIR=/var/lib/mongodb
# /usr/local/bin for mpirun; $RNA_ROOT/scripts for SRHS scripts
export PATH=$PATH:/usr/local/bin:"$RNA_ROOT"/scripts
export RNA_HEADNODE=localhost
export RNA_SI_SERVER=127.0.0.1          # scheduler interface server/port
export RNA_SI_PORT=8888
export RNA_DS_SERVER=127.0.0.1          # datastore server/port
export RNA_DS_PORT=27017
export RNA_DBE_PORT=8889

#!/bin/bash

# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# prevents errors in a pipeline from being masked
set -o pipefail

# Error count
err_cnt=0

# Helper functions
echoerr()
{ 
    echo "$1" 1>&2;
}

check_function()
{
    if ! type  "$1" &> /dev/null; then
        err_cnt=$((err_cnt+1))
        echo "WARNING: $1 doesn't exist. We won't be able to get some data about your system"
        return 1
    fi
}

check_file()
{
    if [ ! -f "$1" ]; then
        err_cnt=$((err_cnt+1))
        echo "WARNING: File $1 does not exist. We won't be able to get some data about your system"
        return 1
    fi
}

# helper functions for Linux system information 
get_processor_name()
{
    awk -F':' '/^model name/ {print $2}' /proc/cpuinfo | uniq | sed -e 's/^[ \t]*//'
}


get_memory_usage()
{
    free -m | awk 'NR==2{printf "%s/%sMB (%.2f%%)\n", $3,$2,$3*100/$2 }'
}

get_disk_usage()
{
    df -h | awk '$NF=="/"{printf "%d/%dGB (%s)\n", $3,$2,$5}'
}

get_current_cpu_usage()
{
    awk '{u=$2+$4; t=$2+$4+$5; if (NR==1){u1=u; t1=t;} else print ($2+$4-u1) * 100 / (t-t1) "%"; }' <(grep 'cpu ' /proc/stat) <(sleep 1;grep 'cpu ' /proc/stat)
}

get_overall_cpu_usage()
{
    grep 'cpu ' /proc/stat | awk '{usage=($2+$4)*100/($2+$4+$5)} END {print usage "%"}'
}

get_os()
{
    hostnamectl | grep "Operating System"
}

get_date()
{
    date +'%Y-%m-%d %T'
}

get_grep_bitcoind()
{
    ps_out=$(ps aux)
    ps_err=$?
    if [[ ps_err -eq 0 ]]; then
        echo "$ps_out" | ( grep bitcoind; if [[ $? -eq 0 || $? -eq 1 ]]; then exit 0; else exit 1; fi ) | awk '
        BEGIN { ORS = ""; print " ["}
        { printf "%s{\"user\": \"%s\", \"pid\": \"%s\", \"cpu\": \"%s\", \"mem\": \"%s\", \"command\": \"%s\"}",
            separator, $1, $2, $3, $4, $11
        separator = ", "
        }
        END { print "] " }'
    fi
}

# Helper function to execute command specified by second (and subseqent) parameters and capture its standard output.
# First paremeter is output in case of error. If command succeeds, captured standard output is written unmodified.
do_cmd()
{
    # Save alternative text in case of error
    alternative=$1
    shift

    # execute command and save error code
    result=$("$@")
    err=$?

    if [[ $err -ne 0 ]]; then
        echo -n "$alternative"
        err_cnt=$((err_cnt+1))
        echoerr "Following command failed: '$*' (stdout='$result')"
    else
        echo -n "$result"
    fi
}

#Default values
BITCOINCLI="bitcoin-cli"
RPC_ADDRESS="127.0.0.1"

# Parse parameters
while [[ $# -gt 0 ]]
do
key="$1"
shift

case $key in
    --rpcuser)
    RPC_USER="$1"
    shift
    ;;
    --rpcpassword)
    RPC_PASSWORD="$1"
    shift
    ;;
    --rpcport)
    RPC_PORT="$1"
    shift
    ;;
    --rpcaddress)
    RPC_ADDRESS="$1"
    shift
    ;;
    --bitcoincli)
    BITCOINCLI="$1"
    shift
    ;;
    --dontdumpconfig)
    DONTDUMPCONFIG=YES
    ;;
    --test)
    TEST=YES
    ;;
    --help)
    HELP=YES
    ;;
    *)    # unknown option
    UNKNOWN_OPTION+=("$key")
    echoerr "ERROR: Unknown option: $UNKNOWN_OPTION"
    exit 1
    ;;
esac
done

if [[ $HELP == YES ]]; then
    echo "Bitcoin diagnostic help"
    echo ""
    echo "Parameters: "
    echo "      --rpcuser       (default=\"\")             RPC user"
    echo "      --rpcpassword   (default=\"\")             RPC password"
    echo "      --rpcport       (default=\"\")             RPC port"
    echo "      --rpcaddress    (default=\"127.0.0.1\")    address where BitcoinSV node runs (only needed when not running for local machine)"
    echo "      --bitcoincli    (default=\"bitcoin-cli\")  name of the bitcoin-cli executable set in the PATH or absolute path to the bitcoin-cli executable"
    echo "      --dontdumpconfig                         If this parameter is used, config settings won't be part of the output"
    echo "      --test                                   Checks if RPC connection is successful and all required linux programs and files exist (if running locally)"
    echo "      --help                                   Get help and examples for bitcoin-diag.sh"
    echo ""
    echo "Examples: "   
    echo "      Running test on local machine and bitcoin-cli is in the directory: my_node/bitcoin_binaries"
    echo "          bitcoin-diag.sh --rpcuser test --rpcpassword test --rpcport 8899 --bitcoincli my_node/bitcoin_binaries/bitcoin-cli --test"
    echo "      Running diag script on local machine and bitcoin-cli is in the PATH"
    echo "          bitcoin-diag.sh --rpcuser test --rpcpassword test --rpcport 8899 > report.json"
    echo "      Running diag script on remote machine and bitcoin-cli is in the PATH (on local machine)"
    echo "          bitcoin-diag.sh --rpcuser test --rpcpassword test --rpcport 8899 --rpcaddress 123.123.123.123 > report.json"
    echo ""
    exit 0
fi

if [[ $TEST == YES ]]; then
    echo "========== TEST CONNECTION =========="
    echo "RPC USER                  = $RPC_USER"
    echo "RPC PASSWORD              = $RPC_PASSWORD"
    echo "RPC PORT                  = $RPC_PORT"
    echo "RPC ADDRESS               = $RPC_ADDRESS"
    echo "BITCOINCLI                = $BITCOINCLI"
    if check_function "$BITCOINCLI"; then
        echo "BITCOINCLI_CHECK          = OK"
    else
        if [ -f "$BITCOINCLI" ]; then
            echo "BITCOINCLI_CHECK          = $BITCOINCLI DOES NOT HAVE EXECUTE PERMISSIONS!"
        else
            echo "BITCOINCLI_CHECK          = $BITCOINCLI DOES NOT EXIST!"
        fi
    fi

    $BITCOINCLI -rpcuser=$RPC_USER -rpcpassword=$RPC_PASSWORD -rpcport=$RPC_PORT -rpcconnect=$RPC_ADDRESS uptime &>/dev/null
    EC=$?
    if [[ $EC -eq 0 ]]; then
        echo "RPC_CONNECTION            = OK"
    else
        err_cnt=$((err_cnt+1))
        echo "RPC_CONNECTION            = FAILED"
    fi

    # Test other (linux) dependencies if running on local machine
    if [[ "$RPC_ADDRESS" == "127.0.0.1" || "$RPC_ADDRESS" == "localhost" ]]; then
        echo ""
        check_function date
        check_file "/sys/class/dmi/id/chassis_vendor"
        check_file "/sys/class/dmi/id/product_name"
        check_file "/sys/class/dmi/id/product_version"
        check_function hostnamectl
        check_function grep
        check_function uname
        check_function arch
        check_function awk
        check_function uniq
        check_function sed
        check_file "/proc/cpuinfo"
        check_function hostname
        check_function free
        check_function df
        check_file "/proc/stat"
        check_function ps
    fi

    # Check error count and exit with appropriate code
    if [ "$err_cnt" -gt 0 ] ; then
        echoerr "Error(s): $err_cnt"
        exit 1
    else
        exit 0
    fi
fi

echo "{"
#========== SYSTEM INFO ==========
echo "\"system_info\": {"
if [[ "$RPC_ADDRESS" == "127.0.0.1" || "$RPC_ADDRESS" == "localhost" ]]; then
    echo -n '"Date": "'; do_cmd 'ERROR' get_date; echo '",'
    echo -n '"Manufacturer": "'; do_cmd 'ERROR' cat /sys/class/dmi/id/chassis_vendor; echo '",'
    echo -n '"Product Name": "'; do_cmd 'ERROR' cat /sys/class/dmi/id/product_name; echo '",'
    echo -n '"Version": "'; do_cmd 'ERROR' cat /sys/class/dmi/id/product_version; echo '",'
    echo -n '"Operating System": "';  do_cmd 'ERROR' get_os; echo '",'
    echo -n '"Kernel": "';  do_cmd 'ERROR' uname -r; echo '",'
    echo -n '"Architecture": "'; do_cmd 'ERROR' arch; echo '",'
    echo -n '"Processor Name": "'; do_cmd 'ERROR' get_processor_name; echo '",'
    echo -n '"IP addresses": "'; do_cmd 'ERROR' hostname -I; echo '",'
    echo -n '"Memory usage": "'; do_cmd 'ERROR' get_memory_usage; echo '",'
    echo -n '"Disk usage": "';  do_cmd 'ERROR' get_disk_usage; echo '",'
    echo -n '"Current CPU usage": "'; do_cmd 'ERROR' get_current_cpu_usage; echo '",'
    echo -n '"Overal CPU usage": "';  do_cmd 'ERROR' get_overall_cpu_usage; echo '",'
    echo '"RPC executed on": "local machine" ,'
    echo -n '"node_processes": '; do_cmd '"ERROR"' get_grep_bitcoind; echo ''
else
    echo -n '"Date": "'; do_cmd 'ERROR' get_date; echo '",'
    echo "\"RPC executed on\": \"remote machine: $RPC_ADDRESS\""
fi
echo "},"

#========== BITCOIN INFO ==========
echo "\"RPC\" :{"

BITCOIN_CLI_ARGUMENTS="-rpcuser=$RPC_USER -rpcpassword=$RPC_PASSWORD -rpcport=$RPC_PORT -rpcconnect=$RPC_ADDRESS"

echo -n '"getbestblockhash": "'; do_cmd "ERROR" $BITCOINCLI $BITCOIN_CLI_ARGUMENTS  getbestblockhash; echo '",'
echo -n '"getblockchaininfo":';  do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getblockchaininfo; echo ','
echo -n '"getblockcount": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getblockcount; echo ','
echo -n '"getchaintips": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getchaintips; echo ','
echo -n '"getdifficulty": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getdifficulty;  echo ','
echo -n '"getmempoolinfo":'; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getmempoolinfo; echo ','
echo -n '"getrawnonfinalmempool": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getrawnonfinalmempool; echo ','
echo -n '"gettxoutsetinfo": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS gettxoutsetinfo;  echo ','
echo -n '"getinfo": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getinfo; echo ','
echo -n '"getmemoryinfo": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getmemoryinfo; echo ','
echo -n '"uptime": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS uptime; echo ','
echo -n '"getmininginfo": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getmininginfo; echo ','
echo -n '"getconnectioncount" :'; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getconnectioncount;  echo ','
echo -n '"getexcessiveblock": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getexcessiveblock; echo ','
echo -n '"getnettotals": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getnettotals; echo ','
echo -n '"getnetworkinfo": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getnetworkinfo; echo ','
echo -n '"getpeerinfo": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getpeerinfo; echo ','
echo -n '"listbanned": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS listbanned; echo ','
if [[ -z $DONTDUMPCONFIG ]]; then
    echo -n '"dumpparameters": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS dumpparameters; echo ','
fi
echo -n '"getsettings": '; do_cmd '"ERROR"' $BITCOINCLI $BITCOIN_CLI_ARGUMENTS getsettings; echo ''
echo "}" # end of RPC

echo "}"

 # Check error count and exit with appropriate code
if [ "$err_cnt" -gt 0 ] ; then
    echoerr "Error(s): $err_cnt"
    exit 1
else
    exit 0
fi

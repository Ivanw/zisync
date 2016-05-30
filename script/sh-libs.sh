#!/bin/bash

#get work directory
if [ "x$WORK_PATH" == "x" ]; then
    #full path
    SCRIPT_PATH=$PWD
    WORK_PATH="$SCRIPT_PATH/.."

    INST_LOG="$SCRIPT_PATH/install.log"

    if which apt-get &>/dev/null ; then
        INSTALL="apt-get"
    elif which yum &>/dev/null ; then
        INSTALL="yum"
    fi

    _init()
    {
        echo "" > $INST_LOG
    }

    #functions
    _get_param()
    {
        # $1 param string; $2 file
        echo `grep -e "^$1=" $2 | cut -f 2 -d '='`
    }

    _execute()
    {

        echo ">> $1"
        echo ">> $1" >> $INST_LOG
        $1 2>> $INST_LOG

        if [ $? -ne 0 ]; then
            echo ">> failed: execute cmd failed!! check log $INST_LOG" >&2
            exit $?
        fi

    }

    ## link command execute function (record link file for deleting when uninstall.sh)
    EXEC_LINK_INFO="/tmp/.tmp.execln.stdout"
    _execute_ln()
    {
        _result=""

        echo $1 | awk -F " " '{print $NF}' >> $EXEC_LINK_INFO

        echo ">> $1"
        echo ">> $1" >> $INST_LOG
        _result=`$1 2>> $INST_LOG`

        echo -n $_result

        if [ $? -ne 0 ]; then
            echo ">> failed: execute cmd failed!! check log $INST_LOG" >&2
            exit $?
        fi
    }


    _execute2()
    {
        # without stdout
        echo ">> $1" >> $INST_LOG
        echo -n `$1 2>> $INST_LOG`

        if [ $? -ne 0 ]; then
            echo ">> failed: execute cmd failed!! check log $INST_LOG" >&2
            exit $?
        fi
    }

    EXEC3_STDOUT="/tmp/.tmp.exec3.stdout"
    EXEC5_STDOUT="/tmp/.tmp.exec5.stdout"
    _execute3()
    {
        # no return
        echo ">> $1 > $EXEC3_STDOUT"
        echo ">> $1" >> $INST_LOG
        $1 > $EXEC3_STDOUT

        if [ $? -ne 0 ]; then
            echo ">> failed: execute cmd failed!! check log $INST_LOG" >&2
            exit $?
        fi
    }

    _execute5()
    {
        # no return
        echo ">> $1 > $EXEC5_STDOUT"
        echo ">> $1" >> $INST_LOG
        `$1 > $EXEC5_STDOUT`

        if [ $? -ne 0 ]; then
            echo ">> failed: execute cmd failed!! check log $INST_LOG" >&2
            exit $?
        fi
    }


    _execute_ne()
    {
        # just print, not execute
        echo ">> $1"
        echo ">> $1" >> $INST_LOG
    }


    _read_param_from_file()
    {
        # $1 file
        _result=""
        while read _line; do
            _first_letter=${_line:0:1}
            if [ "x"$_first_letter == "x#" ]; then
                continue;
            fi

            if [ "x"$_first_letter == "x" ]; then
                continue;
            fi

            _result="$_result $_line"
        done < $1

        echo $_result
    }

    _msg()
    {
        echo ""
        echo ">>>>> $1"
        echo ">>>>> $1" >> $INST_LOG
    }

    _msg_info()
    {
        echo ">>> $1"
        echo ">>> $1" >> $INST_LOG
    }

    _msg_error()
    {
        echo ">>>>>>> Failed! $1" >&2
        echo ">>>>>>> Failed! $1" >> $INST_LOG
    }


    _add_slash()
    {
       echo `echo $1 |sed 's/\//\\\\\//g'`
    }

    _clean()
    {
        rm $EXEC3_STDOUT 2> /dev/null
        rm $EXEC5_STDOUT 2> /dev/null
        rm $EXEC_LINK_INFO 2> /dev/null
    }

    _get_version()
    {
        local FILE=$WORK_PATH/configure.ac
        echo $(grep AC_INIT $FILE | cut -f 2 -d, | sed "s/\[//g" | sed "s/\]//g")
    }
fi

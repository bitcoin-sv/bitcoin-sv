# Copyright (c) 2024 BSV Blockchain Association
    
lscpu | awk '
/Thread\(s\) per core:/{
    printf("%d", $4)
}

/Core\(s\) per socket:/ {
    printf(" %d", $4)
}

/Socket\(s\):/ {
    printf(" %d", $2)
}
' | {
    read tpc cps sockets
    threads=$((sockets * cps * tpc))
    printf 'Sockets: %d\nCores/Socket: %d\nThreads/Core: %d\nThreads: %d\n\n' \
            $sockets \
            $cps \
            $tpc \
            $threads
    sysbench --threads=$threads cpu run
}


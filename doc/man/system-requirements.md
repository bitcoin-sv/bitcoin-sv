#Bitcoin SV System Requirements

The following system requirements are specified for the current release of Bitcoin SV of which this document is supplied with.
These requirements may not remain suitable for future releases of Bitcoin SV.

###Minimum / Development
For operating a listener node which only operates to follow the most PoW chain and handle small volumes of other tasks (RPC requests as an example) we suggest as a minimum​, the following.

    4 Core/Thread CPU
    6GB of Ram
    10+ Mbit internet connection (up and down) 

Please be aware the above configuration may run into issues if there is a sustained burst of transaction volume. This may result in your node falling behind the current best chain tip, or simply running out of memory and shutting down.

We do not recommend using the above for any production environments. 

###Listener Node

For operating a listener node which is expected to handle a medium volume of workload while maintaining real time sync with the current chain tip we recommend a​ minimum​ of the following.

    8 Core/Thread CPU
    16GB Ram
    100Mbit+ Internet connection (up and down) 
    
###Miners or High Load / TX index enabled Listener Nodes

For operating a listener node which expects a high volume of work or has `txindex` enabled, or a mining operation, we suggest the following as a ​minimum​.

    8-12 Core/Thread CPU
    32GB Ram
    1Gbit+ Internet Connection (up and down)
    
##Additional Recommendations  

The following are suggested configuration changes which can be made in the `bitcoin.conf` file or entered as command line arguments at runtime.

These are especially important to consider for miners or services which rely on instant transaction propagation during high volume periods.

    maxmempool=6000
    maxsigcachesize=250
    maxscriptcachesize=250
    maxorphantx=10000
    
                 
Webhook Notification Messages
=============================

Double-spends
-------------
The SV node will use webhooks to notify listeners that new double-spends have been detected on alternate chains on the blockchain. The recipient of the webhook message can be configured using the new command line parameter, *dsdetectedwebhookurl*.

The DSD notification message template is shown below:

```
{
    "version" : number,
    "blocks : [
        {
 
            "divergentBlockHash" : string,
            "headers" : [
                {
                    "version" : number,
                    "hashPrevBlock" : string,
                    "hashMerkleRoot" : string,
                    "time" : number,
                    "bits" : number,
                    "nonce" : number
                }
            ],
            "merkleProof" : {
                "index" : number,
                "txOrId" : string,    // Full transaction, serialised, hex-encoded
                "targetType": "merkleRoot",
                "target" : string,    // Merkle-root
                "nodes" : [ "hash", ... ]
            }
        },
        ...
    ]
}
```

Safe Mode
---------
The node will use webhooks to notify listeners that node has gone into safe-
mode. The recipient of the webhook message can be configured using the new
command line parameter, *safemodewebhookurl*.  The safe-mode notification
message template is shown below.

```
{
    "safemodeenabled": <true/false>,
    "activetip": {
        "hash": "<block_hash>",
        "height": <height>,
        "blocktime": "<time UTC>",
        "firstseentime": "<time UTC>",
        "status": "active"
    },
    "timeutc": "<time_of_the_message>",
    "reorg": {
        "happened": <true/false>,
        "numberofdisconnectedblocks": <number>,
        "oldtip": {
            "hash": "<block_hash>",
            "height": <height>,
            "blocktime": "<time UTC>",
            "firstseentime": "<time UTC>",
            "status": "<block_header_status>"
        }
    },
    "forks": [
        {
            "forkfirstblock": {
                "hash": "<block_hash>",
                "height": <height>,
                "blocktime": "<time UTC>",
                "firstseentime": "<time UTC>",
                "status": "<block_header_status>"
            },
            "tips": [
                {
                    "hash": "<block_hash>",
                    "height": <height>,
                    "blocktime": "<time UTC>",
                    "firstseentime": "<time UTC>",
                    "status": "<block_header_status>"
                },
                ...
            ],
            "lastcommonblock": {
                "hash": "<block_hash>",
                "height": <height>,
                "blocktime": "<time UTC>",
                "firstseentime": "<time UTC>",
                "status": "active"
            },
            "activechainfirstblock": {
                "hash": "<block_hash>",
                "height": <height>,
                "blocktime": "<time UTC>",
                "firstseentime": "<time UTC>",
                "status": "active"
            },
        },
    ...
    ]
}


```

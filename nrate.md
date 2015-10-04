# Networked Ratings (nrate) #
nsdci/dcicommon/nrate.c

## Introduction ##
The Networked Ratings API (nrate) keeps track of counts and totals and determines the average to a given precision.  The API is only exposed when properly configured. See the "Configuration" section for more information.

Additional Reading: [rpc](rpc.md)

## TCL API ##
The API is only exposed when properly configured. nrates.`*` commands are only available on the nrate server, and nratec.`*` commands are only available on the client.

### nrates.find ###
Used to return a list of keys in the  nrate database file.  If a pattern is given glob-style patten matching is used. Server only.

**nrates.find** _?pattern?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _?pattern?_  | String. Optional. The glob-style pattern to use. |


| **Result** | **Description** |
|:-----------|:----------------|
| `TCL_LIST` | Success. All the keys in the db file or the keys that matched _?pattern?_ |

| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nrates.backup ###
Backup the nrate database file. Relative file paths are relative to the server's $home directory.

**nrates.backup** backupFile ?maxroll?

| **Argument** | **Description** |
|:-------------|:----------------|
| _backupFile_ | String. The name of the backup file. |
| _?maxroll?_  | Integer. The number of backup files to keep. File names are appended with a number. 999 is the max. |


| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The nrarte db file was backed up. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nrates.database ###
Returns the path to the nrate databse file.

**nrates.database**

| **Return** | **Desription** |
|:-----------|:---------------|
| `String`   | Success. The path to the nrate database file. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nrates.delete ###

### nrates.debug, nratec.debug ###
Used to retrieve the current debug value, or to set a new debug value.

**nrates.debug** _?boolean?_ , **nratec.debug** _?boolean?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _?boolean?_  | Boolean. Optional. Specifies whether or not debugging is enabled. |


| **Result** | **Description** |
|:-----------|:----------------|
| `1`        | Success. Returned if a boolean was specified. |
| `BOOLEAN`  | Success. If a boolean was not specified, then the current debug value is returned. |

```
--------------------------------------------------------------------------------
```


### nrates.add ###

## Configuration ##

## Best Practices ##
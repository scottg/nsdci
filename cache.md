# Cache API (cache) #
nsdci/dcicommon/cache.c

## Introduction ##
The Cache API provides a mechanism to create size-based and time-based named caches.  The Network Cache Flush API ([ncf](ncf.md)) is used to send cache flush messages.  Together they provide a powerful, high performance cache-messaging service.  The Cache API functions at a very low level and great care should be taken in its use. See the  "Best Practices" section for examples of how to effectively use the Cache API.

## TCL API ##
```
--------------------------------------------------------------------------------
```
### cache.createEntry ###
Creates an entry handle in _cacheName_ identified by _key_.  _newVariable_ is updated with 1 if an entry for the given key already exists. A handle to the entry is returned.

**cache.createEntry** _cacheName key newVariable_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cacheName_  | String. The name of the cache bucket. |
| _key_        | String. The key name to be used. |
| _newVariable_ | String. Variable name to use for the new entry indicator. |


| **Return** | **Description** |
|:-----------|:----------------|
| `String`   | Success. The handle of the entry. |
| `newVariable` | Boolean. 1 if an entry for the given key already exists, 0 if not. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.createSized ###
Creates a named cache, pruned by size. **NOTE:** The Cache API allows the creation of multiple caches of the same name, essentially overwriting the previous cache causing a memory leak.

**cache.createSized** _cacheName size_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cacheName_  | String. The name of the cache bucket. |
| _size_       | Numeric. The size of the bucket in bytes. |


| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The cache bucket was created. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.createTimed ###
Creates a named cache, pruned by time. **NOTE:** The Cache API allows the creation of multiple caches of the same name, essentially overwriting the previous cache causing a memory leak.

**cache.createSized** _cacheName timeout_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cacheName_  | String. The name of the cache bucket. |
| _timeout_    | Integer. The TTL in minutes. |


| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The cache bucket was created. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.deleteEntry ###
Removes an entry from the named cache. **NOTE:** This command does not free the memory associated with the cache entry's value. To delete an entry and free its current value, you must call _cache.flushEntry_.

**cache.deleteEntry** _entry_

| **Argument** | **Description** |
|:-------------|:----------------|
| _entry_      | String. The handle returned by _cache.createEntry_ |


| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The entry was removed from the named cache. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.findEntry ###
Finds a cache entry identified by _key_ in _cacheName_. A handle to this entry is returned.

**cache.findEntry** _cacheName key_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cacheName_  | String. The name of the cache. |
| _key_        | String. The identifing key in the named cache. |


| **Return** | **Description** |
|:-----------|:----------------|
| `String`   | Success. The handle of the cache entry.|
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.flushEntry ###
Deletes an entry from the named cache after first unsetting the current value (if any).

**cache.flushEntry** _entry_

| **Argument** | **Description** |
|:-------------|:----------------|
| _entry_      | String. The handle to the value returned by _cache.findEntry_. |

| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The value was unset, the memory released, and the entry removed. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.getValue ###
Retrieves the string value of an entry handle returned by _cache.findEntry_ The dataVariable argument is then updated with the value if the value is not NULL. If the value is not NULL a "1" is returned, else "0".

**cache.getValue** _entry dataVariable_

| **Argument** | **Description** |
|:-------------|:----------------|
| _entry_      | String. The handle to the value returned by _cache.findEntry_. |
| _dataVariable_| String. The name of the variable to set the result in. |


| **Return** | **Description** |
|:-----------|:----------------|
| `boolean`  | Success. 1 the value was found and _dataVariable_ was set. |
| `dataVariable` | String. This variable is self initializing and only set if the value is not NULL. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.lock ###
Locks the named cache. The lock is an exclusion lock (mutex). **Note:** See "Best Practices" for proper implementation.

**cache.lock** _cacheName_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cacheName_  | String. The name of the cache. |

| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The cache has been locked. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.setValue ###
Updates the cache entry's value, freeing any existing value.

**cache.setValue** _entry value_

| **Argument** | **Description** |
|:-------------|:----------------|
| _entry_      | String. The handle returned by _cache.createEntry_ or _cache.findEntry_. |
| _value_      | String. The value to set. |


| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The value was set. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.unlock ###
Unlocks the named cache. **Note:** See "Best Practices" for proper implementation.

**cache.unlock** _cacheName_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cacheName_  | String. The name of the cache. |

| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The cache has been unlocked. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### cache.unsetValue ###
Unsets the string value of a given cache entry, freeing the data and resetting the value to NULL. This does not delete the entry itself.

| **Argument** | **Description** |
|:-------------|:----------------|
| _entry_      | String. The handle returned by _cache.createEntry_ or _cache.findEntry_. |


| **Return** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The value was unset. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

cache.wait cache.timedWait cache.broadcast cache.signal

## Usage ##
The following [Control Port](http://code.google.com/p/aolserver/wiki/nscp) session is meant to give an overview only. Please see the "Best Practices" section on how to safely use the Cache API.
```
    frontend:nscp 1> cache.createSized myCache 5000000          

    frontend:nscp 2> cache.lock myCache

    frontend:nscp 3> cache.createEntry myCache foo existsBoolean
    eid0x619088
    frontend:nscp 4> cache.setValue eid0x619088 "my value"

    frontend:nscp 5> cache.unlock myCache

    frontend:nscp 6> cache.findEntry myCache foo
    eid0x619088
    frontend:nscp 7> cache.getValue eid0x619088 returnVar
    1
    frontend:nscp 8> set returnVar
    my value
```

## Best Practices ##
Creating a cache entry and setting a value in that entry MUST be made as a single transaction. An exclusion lock is provided to ensure this.  If an error is thrown during this action, the cache would remain locked.  A catch should be used around these actions to ensure proper unwinding of the entry and the lock:
```
    if {[catch {
        cache.lock $cacheName
        set entry [cache.createEntry $cacheName $key existsBoolean]
        cache.setValue $entry $value
        cache.unlock $cacheName
    }]} {
        set errorString ${::errorInfo}
        catch {cache.flushEntry $entry}
        catch {cache.unlock $cacheName}
        error $errorString
    }
```

Special attention should be paid to when and how named caches are created.  The Cache API allows the creation of multiple caches of the same name, essentially overwriting the previous cache causing a memory leak.  Because of this, the safest place for cache creation is at server startup, not in a procedure or page code.

Need to document the BPs for wait, broadcast, and signal
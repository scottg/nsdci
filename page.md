# Page Value (page) #
nsdci/dcicommon/page.c

## Introduction ##
The Page Value api (page) is used as a "global array". It is available to all adp call frames within a connection. It can be used to create data structures at the beginning of a connection to make available later in the connection, keep state during a connection, or pass values between call frames.  It does not keep state between connections.

## TCL API ##
```
--------------------------------------------------------------------------------
```
### page.getValue ###
Used to get the value of _key_. If _key_ does not exist, an empty string is returned.

**page.getValue** _key_

| **Argument** | **Description** |
|:-------------|:----------------|
| _key_        | String. The key to be read. |


| **Result** | **Description**|
|:-----------|:|
| `String`   | Success. The value of _key_ |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### page.appendValue ###
Used to append _value_ to _key_.  If _key is not set, it will set_key_to_value_._

**page.appendValue** _key value_

| **Argument** | **Description** |
|:-------------|:----------------|
| _key_        | String. The key to append. |
| _value_      | String. The value to append to _key_. |


| **Result** | **Description**|
|:-----------|:|
| `NULL`     | Success. The key was appened. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### page.setValue ###
Used to set _key_ to _value_. If _key_ exists it will overwrite the value of _key_.

**page.setValue** _key value_

| **Argument** | **Description** |
|:-------------|:----------------|
| _key_        | String. The key to set. |
| _value_      | String. The value to set. |

| **Result** | **Description**|
|:-----------|:|
| `NULL`     | Success. The key was set. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

## Usage ##
The following example shows a page value being set by 2 adp call frames:

#### main.adp ####
```
    <%
        ns_adp_puts "Setting page value \"foo\" to 1..."
        page.setValue foo 1
        ns_adp_include include.inc
        ns_adp_puts "The value of \"foo\" is now: [page.getValue foo]"
    %>
```
#### include.inc ####
```
    <%
        page.setValue foo 2
    %>
```

The output of main.adp would be:

> Setting page value "foo" to 1...
> The value of "foo" is now: 2

## Best Practices ##
The page value api should only be used in adp pages.  Writing TCL procedures that expect page values to be set is bad practice. Always explicitly pass values to procedures as arguments, use TCL upvar, or use a TCL global.
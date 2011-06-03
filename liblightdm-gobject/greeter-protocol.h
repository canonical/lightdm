/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _GREETER_PROTOCOL_H_
#define _GREETER_PROTOCOL_H_

typedef enum
{
    /* Messages from the greeter to the server */
    GREETER_MESSAGE_CONNECT                 = 1,
    GREETER_MESSAGE_START_AUTHENTICATION    = 2,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION = 3,
    GREETER_MESSAGE_LOGIN                   = 4,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION   = 5,
    GREETER_MESSAGE_GET_USER_DEFAULTS       = 6,
    GREETER_MESSAGE_LOGIN_AS_GUEST          = 7,

    /* Messages from the server to the greeter */
    GREETER_MESSAGE_CONNECTED               = 101,
    GREETER_MESSAGE_QUIT                    = 102,
    GREETER_MESSAGE_PROMPT_AUTHENTICATION   = 103,
    GREETER_MESSAGE_END_AUTHENTICATION      = 104,
    GREETER_MESSAGE_USER_DEFAULTS           = 106  
} GreeterMessage;

#endif /* _GREETER_PROTOCOL_H_ */

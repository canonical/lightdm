/*
 * Copyright (C) 2010-2011 Robert Ancell.
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
    GREETER_MESSAGE_LOGIN                   = 2,
    GREETER_MESSAGE_LOGIN_AS_GUEST          = 3,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION = 4,
    GREETER_MESSAGE_START_SESSION           = 5,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION   = 6,

    /* Messages from the server to the greeter */
    GREETER_MESSAGE_CONNECTED               = 101,
    GREETER_MESSAGE_QUIT                    = 102,
    GREETER_MESSAGE_PROMPT_AUTHENTICATION   = 103,
    GREETER_MESSAGE_END_AUTHENTICATION      = 104,
    GREETER_MESSAGE_SELECT_USER             = 107,
    GREETER_MESSAGE_SELECT_GUEST            = 108
} GreeterMessage;

#endif /* _GREETER_PROTOCOL_H_ */

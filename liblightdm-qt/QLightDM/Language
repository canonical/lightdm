/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#ifndef QLIGHTDM_LANGUAGE_H
#define QLIGHTDM_LANGUAGE_H

#include <QString>

class LanguagePrivate;

namespace QLightDM {
    class Language
    {
    public:
        Language(QString &code, QString &name, QString &territory);
        ~Language();
        Language(const Language& other);
        Language &operator=(const Language& other);

        QString code() const;
        QString name() const;
        QString territory() const;
    private:
        LanguagePrivate* d;
    };
};

#endif // QLIGHTDM_LANGUAGE_H

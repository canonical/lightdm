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

#include "language.h"

using namespace QLightDM;

class LanguagePrivate
{
public:
    QString code;
    QString name;
    QString territory;
};

Language::Language(QString &code, QString &name, QString &territory)
    : d(new LanguagePrivate)
{
    d->code = code;
    d->name = name;
    d->territory = territory;
}

Language::Language(const Language &other)
    :d(new LanguagePrivate(*other.d))
{
}

Language::~Language()
{
    delete d;
}

Language& Language::operator=(const Language& other)
{
    *d = *other.d;
    return *this;
}


QString Language::code() const
{
    return d->code;
}

QString Language::name() const
{
    return d->name;
}

QString Language::territory() const
{
    return d->territory;
}

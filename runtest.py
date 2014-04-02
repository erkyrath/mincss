#!/usr/bin/env python

import re
import subprocess

class TrackMetaClass(type):
    def __init__(cls, name, bases, dict):
        super(TrackMetaClass, cls).__init__(name, bases, dict)
        if len(bases) == 1:
            bases[0].map[dict['name']] = cls

class TokenBase:
    __metaclass__ = TrackMetaClass
    map = {}
    name = '???'

    def __init__(self, text=None):
        self.text = text

    def __repr__(self):
        if self.text is None:
            return '<%s>' % (self.name,)
        else:
            return '<%s %s>' % (self.name, repr(self.text),)

class EOF(TokenBase):
    name = 'EOF'
class Delim(TokenBase):
    name = 'Delim'
class Space(TokenBase):
    name = 'Space'
class Comment(TokenBase):
    name = 'Comment'
class Number(TokenBase):
    name = 'Number'
class String(TokenBase):
    name = 'String'
class Ident(TokenBase):
    name = 'Ident'
class AtKeyword(TokenBase):
    name = 'AtKeyword'
class Percentage(TokenBase):
    name = 'Percentage'
class Dimension(TokenBase):
    name = 'Dimension'
class LBrace(TokenBase):
    name = 'LBrace'
class RBrace(TokenBase):
    name = 'RBrace'
class LBracket(TokenBase):
    name = 'LBracket'
class RBracket(TokenBase):
    name = 'RBracket'
class LParen(TokenBase):
    name = 'LParen'
class RParen(TokenBase):
    name = 'RParen'
class Colon(TokenBase):
    name = 'Colon'
class Semicolon(TokenBase):
    name = 'Semicolon'

errorcount = 0

def reporterror(msg):
    global errorcount
    errorcount = errorcount + 1
    print 'ERROR: %s' % (msg,)
    
tokenlinepat = re.compile('^<([A-Za-z]*)> *"(.*)"$')
errorlinepat = re.compile('^MinCSS error: (.*)$')

def lextest(input, wanttokens, wanterrors=[]):
    popen = subprocess.Popen(['./test', '--lexer'],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (stdout, stderr) = popen.communicate(input)

    errors = []
    for ln in stderr.split('\n'):
        match = errorlinepat.match(ln)
        if not match:
            continue
        errors.append(match.group(1))

    tokens = []
    for ln in stdout.split('\n'):
        match = tokenlinepat.match(ln)
        if not match:
            continue
        name = match.group(1)
        val = match.group(2)
        cla = TokenBase.map.get(name)
        if not cla:
            reporterror('token not recognized: %s' % (name,))
            continue
        tokens.append(cla(val))

    print '###', errors
    print '###', tokens
    for (ix, error) in enumerate(errors):
        if ix >= len(wanterrors):
            reporterror('unexpected error: %r' % (error,))
        else:
            wanted = wanterrors[ix]
            if error != wanted:
                reporterror('error mismatch: wanted %r, got %r' % (wanted, error,))
    for wanted in wanterrors[len(errors):]:
        reporterror('failed to get error: %r' % (wanted,))
        

lextest('foo  bar\n', [])

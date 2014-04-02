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

    def match(self, token):
        if self.name != token.name:
            return False
        if self.text is not None:
            if self.text != token.text:
                return False
        return True

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
    if type(input) is unicode:
        input = input.encode('utf-8')
        
    popen = subprocess.Popen(['./test', '--lexer'],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (stdout, stderr) = popen.communicate(input)

    stdout = stdout.decode('utf-8')
    stderr = stderr.decode('utf-8')

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

    for (ix, error) in enumerate(errors):
        if ix >= len(wanterrors):
            reporterror('unexpected error: %r' % (error,))
        else:
            wanted = wanterrors[ix]
            if error != wanted:
                reporterror('error mismatch: wanted %r, got %r' % (wanted, error,))
    for wanted in wanterrors[len(errors):]:
        reporterror('failed to get error: %r' % (wanted,))

    for (ix, token) in enumerate(tokens):
        if ix >= len(wanttokens):
            reporterror('unexpected token: %r' % (token,))
        else:
            wanted = wanttokens[ix]
            if type(wanted) is TrackMetaClass:
                wanted = wanted()
            if not wanted.match(token):
                reporterror('token mismatch: wanted %r, got %r' % (wanted, token,))
    for wanted in wanttokens[len(tokens):]:
        reporterror('failed to get token: %r' % (wanted,))
    
lextestlist = [
    (' \f\t\n\r \n',
     [Space(' ^L^I^J^M ^J')]),
    
    ('()[]{};:!@#$%',
     [LParen, RParen, LBracket, RBracket, LBrace, RBrace, Semicolon, Colon, Delim('!'), Delim('@'), Delim('#'), Delim('$'), Delim('%')]),
    
    ('/* */',
     [Comment('/* */')]),
    (u'/* Whatever\uFB00. */',
     [Comment(u'/* Whatever\uFB00. */')]),
    ('/* * // */ /****/  /* /* */',
     [Comment('/* * // */'), Space, Comment('/****/'), Space, Comment('/* /* */')]),
    ('/* \n */',
     [Comment('/* ^J */')]),
    ('/* *',
     [Comment('/* *')],
     ['Unterminated comment']),
    
    ('foo',
     [Ident('foo')]),
    ('foo bar\n',
     [Ident('foo'), Space(' '), Ident('bar'), Space('^J')]),
    ('-foo123- _0!',
     [Ident('-foo123-'), Space, Ident('_0'), Delim('!')]),
    (u'f\u00E4f \u016c\u13a3\u4e01\ufb00',
     [Ident(u'f\u00E4f'), Space, Ident(u'\u016c\u13a3\u4e01\ufb00')]),
    
    ('@foo @-bar @123',
     [AtKeyword('@foo'), Space, AtKeyword('@-bar'), Space, Delim('@'), Number('123')]),
    (u'@\xE5\uFB00',
     [AtKeyword(u'@\xe5\ufb00')]),
    
    ('1234',
     [Number('1234')]),
    ('12!34/**/',
     [Number('12'), Delim('!'), Number('34'), Comment('/**/')]),
    ('1.51 5. .5 6.',
     [Number('1.51'), Space, Number('5'), Delim('.'), Space, Number('.5'), Space, Number('6'), Delim('.')]),
    ('12..3 1.x3',
     [Number('12'), Delim('.'), Number('.3'), Space, Number('1'), Delim('.'), Ident('x3')]),
    ('1.23.4',
     [Number('1.23'), Number('.4')]),
    
    ('89% .1% .%',
     [Percentage('89%'), Space, Percentage('.1%'), Space, Delim('.'), Delim('%')]),
    ('1.2pt .2x 1-e3',
     [Dimension('1.2pt'), Space, Dimension('.2x'), Space, Dimension('1-e3')]),
    ('3x3y3z 1..2x',
     [Dimension('3x3y3z'), Space, Number('1'), Delim('.'), Dimension('.2x')]),
    (u'300\u1234-\u00e4\n',
     [Dimension(u'300\u1234-\u00e4'), Space('^J')]),

    ('"hello" \'there\'\n',
     [String('"hello"'), Space, String("'there'"), Space]),
    ('"hello',
     [String('"hello')],
     ['Unterminated string']),
    ('"hello\nfoo"',
     [String('"hello^J'), Ident('foo'), String('"')],
     ['Unterminated string', 'Unterminated string']),
    ('"hello\\',
     [String('"hello\\')],
     ['Unterminated string (ends with backslash)']),
    ('"hello\\"foo" \'x\\\'y\'',
     [String('"hello"foo"'), Space, String("'x'y'")]),
    ('"one\\\ntwo\\\rthree\\\r\\\nfour"',
     [String('"onetwothreefour"')]),
    ('"three\\\n\\\rfour"',
     [String('"threefour"')]),
    (u'"x\u0020\u00E4\uFB01y" "\\x\\y\\z"',
     [String(u'"x \u00E4\uFB01y"'), Space, String('"xyz"')]),
    ('"x\\E4y\\E5 z\\fb00"',
     [String(u'"x\u00e4y\u00e5z\ufb00"')]),
    ]

for tup in lextestlist:
    input = tup[0]
    tokens = tup[1]
    errors = []
    if len(tup) == 3:
        errors = tup[2]
    lextest(input, tokens, errors)

if errorcount:
    print 'FAILED, %d errors' % (errorcount,)

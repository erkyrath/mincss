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
class Function(TokenBase):
    name = 'Function'
class Hash(TokenBase):
    name = 'Hash'
class URI(TokenBase):
    name = 'URI'
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
    ('\\',
     [Delim('\\')]),
    ('\\\n',
     [Delim('\\'), Space('^J')]),
    
    ('/* */',
     [Comment('/* */')]),
    (u'/* Whatever\uFB00. */',
     [Comment(u'/* Whatever\uFB00. */')]),
    ('/* * // */ /****/  /* /* */',
     [Comment('/* * // */'), Space, Comment('/****/'), Space, Comment('/* /* */')]),
    ('/* \n */',
     [Comment('/* ^J */')]),
    ('/* \\ */ /* \\n */',
     [Comment('/* \\ */'), Space, Comment('/* \\n */')]),
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
    (u'fo\u0084x  \u009F-i \u007F\u00A0\u00A0',
     [Ident('fo'), Delim(u'\u0084'), Ident('x'), Space, Delim(u'\u009F'), Ident('-i'), Space, Delim(u'\u007F'), Ident(u'\u00A0\u00A0')]),
    ('foo\\',
     [Ident('foo'), Delim('\\')]),
    ('fo\\x\\ny g\\41\\42 \\43q A\\16c\\13a3\\4e01\\fb00',
     [Ident('foxny'), Space, Ident('gABCq'), Space, Ident(u'A\u016c\u13a3\u4e01\ufb00')]),
    ('foo\\\ny',
     [Ident('foo'), Delim('\\'), Space, Ident('y')]),
    ('fn\\6F\\70 \\002F-\\!x X\\\\',
     [Ident('fnop/-!x'), Space, Ident('X\\')]),
    ('\\41zoo \\42\\043 x \\/\\+',
     [Ident('Azoo'), Space, Ident('BCx'), Space, Ident('/+')]),
    ('\\00FB012',
     [Ident(u'\uFB01\u0032')]),
    ('- \\02D',
     [Ident('-'), Space, Ident('-')]),
    
    ('@foo @-bar @123',
     [AtKeyword('@foo'), Space, AtKeyword('@-bar'), Space, Delim('@'), Number('123')]),
    (u'@\xE5\uFB00',
     [AtKeyword(u'@\xe5\ufb00')]),
    
    ('#foo #-bar #123',
     [Hash('#foo'), Space, Hash('#-bar'), Space, Hash('#123')]),
    ('#a #\\42 \\43',
     [Hash('#a'), Space, Hash('#BC')]),
    ('#. #A. #\\2E  ## ##X',
     [Delim('#'), Delim('.'), Space, Hash('#A'), Delim('.'), Space, Hash('#.'), Space, Delim('#'), Delim('#'), Space, Delim('#'), Hash('#X')]),
    
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
    ('1.2.3% 1.2x3% 1.%',
     [Number('1.2'), Percentage('.3%'), Space, Dimension('1.2x3'), Delim('%'), Space, Number('1'), Delim('.'), Delim('%')]),
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
    ('"x\\41y\\042 z\\0043\n\\44\f\\45\ry"',
     [String('"xAyBzCDEy"')]),
    ('"x\\41\r\ny z\\42\n z"',
     [String('"xAy zB z"')]),
    ('"hi\\41',
     [String('"hiA')],
     ['Unterminated string']),
    ('"hi\\41 ',
     [String('"hiA')],
     ['Unterminated string']),
    ('"hi\\41\r',
     [String('"hiA')],
     ['Unterminated string']),

    ('Foo()',
     [Function('Foo('), RParen]),
    ('.( Foo.( Bar\\2E(',
     [Delim('.'), LParen, Space, Ident('Foo'), Delim('.'), LParen, Space, Function('Bar.(')]),
    ('A\\42\\043\\X(',
     [Function('ABCX(')]),
    ('\\42\\043\\X( \\Y(',
     [Function('BCX('), Space, Function('Y(')]),
    ('A\\( B\\((',
     [Ident('A('), Space, Function('B((')]),
    ('func(5) \!bar("xy")',
     [Function('func('), Number('5'), RParen, Space, Function('!bar('), String('"xy"'), RParen]),
    
    ('url("http://x") url(http://x/y)',
     [URI('url("http://x")'), Space, URI('url(http://x/y)')]),
    ('url curl urli',
     [Ident('url'), Space, Ident('curl'), Space, Ident('urli')]),
    ('curl("x") Url("x") URL("(")',
     [Function('curl('), String('"x"'), RParen, Space, URI('Url("x")'), Space, URI('URL("(")')]),
    ('url(\n" "\n)',
     [URI('url(^J" "^J)')]),
    (u'url( \'x\'\t) url("\\61:\u00E4")',
     [URI('url( \'x\'^I)'), Space, URI(u'url("a:\u00E4")')]),
    ('url "x"',
     [Ident('url'), Space, String('"x"')]),
    ('url()',
     [Function('url('), RParen]),
    ('url() url(() url(   )',
     [Function('url('), RParen, Space, Function('url('), LParen, RParen, Space, Function('url('), Space, RParen]),
    ('url(',
     [Function('url(')]),
    ('url( X ',
     [Function('url('), Space, Ident('X'), Space]),
    ('url("xyz" ',
     [Function('url('), String('"xyz"'), Space]),
    ('url(")',
     [Function('url('), String('")')],
     ['Unterminated string', 'Unterminated string']),
    ('url( "x" 3)',
     [Function('url('), Space, String('"x"'), Space, Number('3'), RParen]),
    ('url(x"y")',
     [Function('url('), Ident('x'), String('"y"'), RParen]),
    (u'url(\u00A0) url(\u009F)',
     [URI(u'url(\u00A0)'), Space, Function('url('), Delim(u'\u009F'), RParen]),
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

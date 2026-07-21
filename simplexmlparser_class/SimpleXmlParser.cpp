/********************************************************************************
 *   Copyright (C) 2012-2016 by NetResults S.r.l. ( http://www.netresults.it )  *
 *   Author(s):                                                                 *
 *              Francesco Lamonica		<f.lamonica@netresults.it>              *
 ********************************************************************************/

#include "SimpleXmlParser.h"

#include <QDebug>
#include <QStringList>

namespace {

QString normalizeTagName(const QString &tag)
{
    // Fast path: check if normalization is needed
    bool needsNormalization = false;
    for (int i = 0; i < tag.length(); ++i) {
        const QChar c = tag.at(i);
        if (c == '<' || c == '>') {
            needsNormalization = true;
            break;
        }
    }
    if (!needsNormalization) {
        return tag;
    }

    QString out;
    out.reserve(tag.length());
    for (int i = 0; i < tag.length(); ++i) {
        const QChar c = tag.at(i);
        if (c != '<' && c != '>') {
            out.append(c);
        }
    }
    return out;
}

bool hasNonWhitespaceBefore(const QString &msg, int end)
{
    for (int i = 0; i < end && i < msg.length(); ++i) {
        if (!msg.at(i).isSpace()) {
            return true;
        }
    }
    return false;
}

bool isTagNameBoundary(const QString &msg, int posAfterTagName)
{
    if (posAfterTagName >= msg.length()) {
        return false;
    }

    const QChar separator = msg.at(posAfterTagName);
    return separator.isSpace() || separator == '>' || separator == '/';
}

bool findStartTag(const QString &msg,
                      const QString &startTagPrefix,
                      int begin,
                      int &startIdx,
                      int &endIdx,
                      bool &emptyTag)
{
    if (startTagPrefix.length() <= 1) {
        return false;
    }

    int searchFrom = begin;

    while (true) {
        const int idx = msg.indexOf(startTagPrefix, searchFrom, Qt::CaseSensitive);
        if (idx < 0) {
            return false;
        }

        const int afterTagName = idx + startTagPrefix.length();
        if (!isTagNameBoundary(msg, afterTagName)) {
            searchFrom = idx + 1;
            continue;
        }

        const int gtIdx = msg.indexOf('>', afterTagName);
        if (gtIdx < 0) {
            return false;
        }

        int maybeSlash = gtIdx - 1;
        while (maybeSlash > afterTagName && msg.at(maybeSlash).isSpace()) {
            --maybeSlash;
        }

        startIdx = idx;
        endIdx = gtIdx;
        emptyTag = (maybeSlash >= afterTagName && msg.at(maybeSlash) == '/');
        return true;
    }
}

QMap<QString, QString> parseTagProperties(const QString &msg,
                                              const QString &startTagPrefix,
                                              int startIdx,
                                              int endIdx)
{
    QMap<QString, QString> map;

    const int propsStart = startIdx + startTagPrefix.length();
    const int propsLen = endIdx - propsStart;
    if (propsLen <= 0) {
        return map;
    }

    const QString props = msg.mid(propsStart, propsLen).trimmed();
    int i = 0;
    while (i < props.length()) {
        while (i < props.length() && props.at(i).isSpace()) {
            ++i;
        }
        if (i >= props.length()) {
            break;
        }

        const int keyStart = i;
        while (i < props.length()) {
            const QChar c = props.at(i);
            if (c.isLetterOrNumber() || c == '_' || c == '-') {
                ++i;
            }
            else {
                break;
            }
        }
        if (i == keyStart) {
            ++i;
            continue;
        }
        const QString key = props.mid(keyStart, i - keyStart);

        while (i < props.length() && props.at(i).isSpace()) {
            ++i;
        }
        if (i >= props.length() || props.at(i) != '=') {
            continue;
        }
        ++i;

        while (i < props.length() && props.at(i).isSpace()) {
            ++i;
        }
        if (i >= props.length()) {
            break;
        }

        const QChar quote = props.at(i);
        if (quote != '\'' && quote != '"') {
            while (i < props.length() && !props.at(i).isSpace()) {
                ++i;
            }
            continue;
        }
        ++i;

        const int valueStart = i;
        while (i < props.length() && props.at(i) != quote) {
            ++i;
        }
        if (i >= props.length()) {
            break;
        }

        map[key] = props.mid(valueStart, i - valueStart);
        ++i;
    }

    return map;
}

}

/*!
   \class SimpleXmlParser
   \brief this class implements a very simple xml parser that has an hybrid function between SAX and DOM
   \note it does not require the XML module from Qt
   \note it does not yet support attribute parsing (we said it's simple :) )
  */
SimpleXmlParser::SimpleXmlParser(QObject *parent)
    : QObject(parent),
      m_maxBufferSizeInBytes(0)
{
    m_notifyMode = E_NotifyOnly;
}


void
SimpleXmlParser::setMaxBufferSize(int sizeInBytes)
{
    if (sizeInBytes >= 0) {
        m_maxBufferSizeInBytes = sizeInBytes;
    }
}


void
SimpleXmlParser::setStartTag(const QString &aTag)
{
    m_StartTag = aTag;
    m_cachedStartTagOpen = "<" + aTag + ">";
    m_cachedStartTagClose = "</" + aTag + ">";
}


QString
SimpleXmlParser::getCurrentBuffer() const
{
    return m_buffer;
}



void
SimpleXmlParser::emptyBuffer()
{
    m_buffer.clear();
}



QString
SimpleXmlParser::decodeEntities(const QString &s)
{
    // Single-pass decode: scan once, build result
    QString ret;
    ret.reserve(s.length());

    int i = 0;
    while (i < s.length()) {
        const QChar c = s.at(i);

        // Handle replacement char
        if (c == QChar(0xFFFD)) {
            ret.append(' ');
            ++i;
            continue;
        }

        // Check for entity
        if (c != '&') {
            ret.append(c);
            ++i;
            continue;
        }

        // Try to match entity starting at i
        const int remaining = s.length() - i;

        // Check named entities (longest first to avoid partial matches)
        if (remaining >= 6 && s.at(i+1) == 'a' && s.at(i+2) == 'p' && s.at(i+3) == 'o' && s.at(i+4) == 's' && s.at(i+5) == ';') {
            ret.append('\'');
            i += 6;
            continue;
        }
        if (remaining >= 6 && s.at(i+1) == 'q' && s.at(i+2) == 'u' && s.at(i+3) == 'o' && s.at(i+4) == 't' && s.at(i+5) == ';') {
            ret.append('"');
            i += 6;
            continue;
        }
        if (remaining >= 5 && s.at(i+1) == 'a' && s.at(i+2) == 'm' && s.at(i+3) == 'p' && s.at(i+4) == ';') {
            ret.append('&');
            i += 5;
            continue;
        }
        if (remaining >= 4 && s.at(i+1) == 'g' && s.at(i+2) == 't' && s.at(i+3) == ';') {
            ret.append('>');
            i += 4;
            continue;
        }
        if (remaining >= 4 && s.at(i+1) == 'l' && s.at(i+2) == 't' && s.at(i+3) == ';') {
            ret.append('<');
            i += 4;
            continue;
        }

        // Check numeric entity &#...;
        if (remaining >= 4 && s.at(i + 1) == '#') {
            const int afterHash = i + 2;
            bool isHex = (s.at(afterHash) == 'x' || s.at(afterHash) == 'X');
            int digitStart = isHex ? afterHash + 1 : afterHash;
            int pos = digitStart;

            while (pos < s.length()) {
                const QChar dc = s.at(pos);
                if (isHex) {
                    if ((dc >= '0' && dc <= '9') || (dc >= 'a' && dc <= 'f') || (dc >= 'A' && dc <= 'F')) {
                        ++pos;
                    } else {
                        break;
                    }
                } else {
                    if (dc >= '0' && dc <= '9') {
                        ++pos;
                    } else {
                        break;
                    }
                }
            }

            if (pos > digitStart && pos < s.length() && s.at(pos) == ';') {
                // Compute codepoint inline without allocating a QString
                int codePoint = 0;
                bool ok = true;
                for (int d = digitStart; d < pos; ++d) {
                    const ushort ch = s.at(d).unicode();
                    int digit;
                    if (ch >= '0' && ch <= '9') {
                        digit = ch - '0';
                    } else if (isHex && ch >= 'a' && ch <= 'f') {
                        digit = ch - 'a' + 10;
                    } else if (isHex && ch >= 'A' && ch <= 'F') {
                        digit = ch - 'A' + 10;
                    } else {
                        ok = false;
                        break;
                    }
                    codePoint = codePoint * (isHex ? 16 : 10) + digit;
                }
                if (ok && codePoint > 0) {
                    ret.append(QChar(codePoint));
                    i = pos + 1;
                    continue;
                }
            }
        }

        // Not a recognized entity, keep the &
        ret.append(c);
        ++i;
    }

    return ret;
}



QString
SimpleXmlParser::encodeEntities(const QString &s, bool encodeNonAscii)
{
    // Single-pass encode: scan once, build result
    QString ret;
    ret.reserve(s.length() + s.length() / 4); // estimate ~25% expansion

    for (int i = 0; i < s.length(); ++i) {
        const QChar c = s.at(i);
        const ushort u = c.unicode();

        if (c == '&') {
            ret.append(QLatin1String("&amp;"));
        } else if (c == '<') {
            ret.append(QLatin1String("&lt;"));
        } else if (c == '>') {
            ret.append(QLatin1String("&gt;"));
        } else if (c == '"') {
            ret.append(QLatin1String("&quot;"));
        } else if (c == '\'') {
            ret.append(QLatin1String("&apos;"));
        } else if (encodeNonAscii && u > 128) {
            ret.append(QLatin1String("&#x"));
            ret.append(QString::number(u, 16));
            ret.append(';');
        } else {
            ret.append(c);
        }
    }

    return ret;
}





/*!
  \brief this is a commodity function that parses a string looking for an xml tag and returns what is inside
  \param i_msg the message to parse
  \param i_tag the tag we want to find and parse
  \param i_offset the initial offset we should start looking the tag from, its default is 0
  \return the string contained within the found tag
  \note we assume the tag ALWAYS exists
  */
QString
SimpleXmlParser::getTagValue(const QString & i_msg, const QString & i_tag, int i_offset, QString defaultValue)
{
    const QString tagname = normalizeTagName(i_tag);
    if (tagname.isEmpty()) {
        return defaultValue;
    }

    const QString startTagPrefix = "<" + tagname;
    const QString endtag = "</" + tagname + ">";

    int idx = -1;
    int endidx = -1;
    bool emptytag = false;
    if (!findStartTag(i_msg, startTagPrefix, i_offset, idx, endidx, emptytag)) {
        return defaultValue;
    }

#ifdef SXML_DBG
    qDebug() << "idx, endix: " << idx << endidx;
#endif
    if (emptytag) {
        return "";
    }

    //it was not empty... go on
    const int valueStart = endidx + 1;
    int idx2 = i_msg.indexOf(endtag, valueStart);
    if (idx < 0 || idx2 < 0)
        return defaultValue;

    QString tag = i_msg.mid(valueStart, idx2 - valueStart);
    return tag;
}



QString
SimpleXmlParser::getDecodedTagValue(const QString &msg, const QString &tag, int beginidx, QString defaultValue)
{
    return decodeEntities(getTagValue(msg, tag, beginidx, defaultValue));
}



/*!
    \brief this method scans the message once and extracts all values for the requested tag
  \param _msg the entire message to parse
  \param _tag the tag we want to gather the values
  \return a list of string containing all the values of the specified tags
  */
QStringList
SimpleXmlParser::getTagsValues(const QString & _msg, const QString & _tag)
{
    QStringList vlist;
    int idx = -1;
    int endidx = -1;
    int last = 0;
    int loopCounter = 0;
    bool emptyTag = false;
    const QString ntag = normalizeTagName(_tag);

    if (ntag.isEmpty()) {
        return vlist;
    }

    const QString startTagPrefix = "<" + ntag;
    const QString endTag = "</" + ntag + ">";

    while (findStartTag(_msg, startTagPrefix, last, idx, endidx, emptyTag)) {
#ifdef SXML_DBG
        qDebug() << "parsing loop " << loopCounter << " idx=" << last;
#endif

        if (emptyTag) {
            vlist << "";
            last = endidx + 1;
        }
        else {
            const int valueStart = endidx + 1;
            const int endTagIdx = _msg.indexOf(endTag, valueStart, Qt::CaseSensitive);
            if (endTagIdx < 0) {
                vlist << "";
                last = valueStart;
            }
            else {
                vlist << _msg.mid(valueStart, endTagIdx - valueStart);
                last = endTagIdx + endTag.length();
            }
        }

        ++loopCounter;
    }

    return vlist;
}



QStringList
SimpleXmlParser::getDecodedTagsValues(const QString &msg, const QString &tag)
{
    QStringList rawValues = getTagsValues(msg, tag);
    QStringList retval;
    retval.reserve(rawValues.size());
    for (const QString &rawVal : rawValues) {
        retval << decodeEntities(rawVal);
    }
    return retval;
}



QMap<QString, QString>
SimpleXmlParser::getTagProperties(const QString &i_msg, const QString &i_tag, int i_offset)
{
    const QString tagname = normalizeTagName(i_tag);
    if (tagname.isEmpty()) {
        return QMap<QString, QString>();
    }

    const QString startTagPrefix = "<" + tagname;
    int idx = -1;
    int endidx = -1;
    bool emptyTag = false;
    if (!findStartTag(i_msg, startTagPrefix, i_offset, idx, endidx, emptyTag)) {
        return QMap<QString, QString>();
    }

    return parseTagProperties(i_msg, startTagPrefix, idx, endidx);
}



QList<QMap<QString, QString> >
SimpleXmlParser::getTagsProperties(const QString &i_msg, const QString &i_tag)
{
    QList<QMap<QString, QString> >maplist;

    const QString ntag = normalizeTagName(i_tag);
    if (ntag.isEmpty()) {
        return maplist;
    }

    const QString startTagPrefix = "<" + ntag;
    int idx = -1;
    int endidx = -1;
    int last = 0;
    int loopCounter = 0;
    bool emptyTag = false;

    while (findStartTag(i_msg, startTagPrefix, last, idx, endidx, emptyTag)) {
#ifdef SXML_DBG
        qDebug() << "parsing loop " << loopCounter << " idx=" << last;
#endif

        maplist << parseTagProperties(i_msg, startTagPrefix, idx, endidx);
        last = endidx + 1;
        ++loopCounter;
    }

    return maplist;
}

/********TEST FNXS *********/

void
SimpleXmlParser::test_getTag()
{
    QString ts1 = "<pippo>ciao</pippo>";
    QString ts1b = "<pippo2>ciao</pippo2>";
    QString ts2 = "<pippo/>";
    QString ts3 = "<pippo   />";
    QString ts4 = "<pippo  >ciao</pippo>";
    QString ts5 = "<pippo p1='bello' >ciao</pippo>";

    QString ts6 = "<pippolist>\
            <pippo>ciao</pippo>\n\
            <pippo>ciao2</pippo>\
            </pippolist>";

    QString ts7 = "<pippo p1='ciao' >alice &lt; bob&#x2019;s mom &amp; '3 &gt; 1' &#233;&#224;&#8364;</pippo>";

    QString rs;
    QStringList rsl;

    rs = SimpleXmlParser::getTagValue(ts1, "pippo");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs=="ciao");
    qDebug() << "Test 1 passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts1b, "pippo");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs!="ciao");
    qDebug() << "Test 1b passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts1, "<pippo>");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs=="ciao");
    qDebug() << "Test 1c passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts2, "pippo");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs=="");
    qDebug() << "Test 2 passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts3, "pippo");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs=="");
    qDebug() << "Test 3 passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts4, "pippo");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs=="ciao");
    qDebug() << "Test 4 passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts5, "pippo");
    qDebug() << "Result: " << rs;
    Q_ASSERT(rs=="ciao");
    qDebug() << "Test 5 passed\n----------\n";

    rsl = SimpleXmlParser::getTagsValues(ts6, "pippo");
    qDebug() << "Result: " << rsl;
    Q_ASSERT(rsl.size()==2);
    Q_ASSERT(rsl.at(0)=="ciao");
    Q_ASSERT(rsl.at(1)=="ciao2");
    qDebug() << "Test 6 passed\n----------\n";

    rsl = SimpleXmlParser::getTagsValues(ts6, "<pippo>");
    qDebug() << "Result: " << rsl;
    Q_ASSERT(rsl.size()==2);
    Q_ASSERT(rsl.at(0)=="ciao");
    Q_ASSERT(rsl.at(1)=="ciao2");
    qDebug() << "Test 6b passed\n----------\n";

    rs = SimpleXmlParser::getTagValue(ts7, "pippo");
    qDebug() << "Result: " << decodeEntities(rs);
    Q_ASSERT(decodeEntities(rs)=="alice < bob’s mom & '3 > 1' éà€");
    qDebug() << "Test 7 passed\n----------\n";
}

void
SimpleXmlParser::test_getProperty()
{
    QString ts1 = "<pippo p1='bello' >ciao</pippo>";
    QString ts2 = "<pippo p1  =  'bello  sguardo' >ciao</pippo>";
    QString ts3 = "<pippo p1=\"bello 'sguardo' \" p2='ciccio'>ciao</pippo>";
    QString ts4 = "<pippolist> <pippo p1=\"bello 'sguardo' \" p2='ciccio'>ciao</pippo><pippo p1=\"bello 'sguardo' \" p3='ciccio2'>ciao</pippo></pippolist>";

    QString ts5 = "<TrapList>\
            <trap\
            eventType='userInput'\
            networkType='eth'\
            isAlice='true'\
            userName='mario.rossi@gmail.com'\
            fwVersion='13.16.00'\
            raVersion='01.11.00'\
            loVersion='027.072.000'\
            timestamp='2013/06/07_14:45:13'>\
            <body\
            eventName='PLAY'\
            videoUrl='http%3A%2F%2Fctv.alice.cdn.interbusiness.it%2FDAM%2FV1%2FFilm%2F2012%2F05%2FRA3_50266398.wmv%7CCOMPONENT%3DWMDRM'\
            videoTitle='Il Gladiatore'\
            />\
            </trap>\
            </TrapList>";

    QString ts6 = "<pippo p1=\"alice &lt; bob&#x2019;s mom &amp; '3 &gt; 1'\" p2='&#233;&#224;&#8364;'>ciao</pippo>";

    QMultiMap<QString, QString>* rm;
    QMultiMap<QString, QString>* rm2;
    QList<QMap<QString, QString> >rmlist;

    rm = new QMultiMap<QString, QString>(SimpleXmlParser::getTagProperties(ts1, "pippo"));
    qDebug() << "Result: " << *rm;
    Q_ASSERT(rm->contains("p1"));
    Q_ASSERT(rm->value("p1")=="bello");
    qDebug() << "Test 1 passed\n----------\n";
    delete rm;

    rm = new QMultiMap<QString, QString>(SimpleXmlParser::getTagProperties(ts2, "pippo"));
    qDebug() << "Result: " << *rm;
    Q_ASSERT(rm->contains("p1"));
    Q_ASSERT(rm->value("p1")=="bello  sguardo");
    qDebug() << "Test 2 passed\n----------\n";
    delete rm;

    rm = new QMultiMap<QString, QString>(SimpleXmlParser::getTagProperties(ts3, "pippo"));
    qDebug() << "Result: " << *rm;
    Q_ASSERT(rm->contains("p1"));
    Q_ASSERT(rm->value("p1")=="bello 'sguardo' ");
    Q_ASSERT(rm->contains("p2"));
    Q_ASSERT(rm->value("p2")=="ciccio");
    qDebug() << "Test 3 passed\n----------\n";
    delete rm;

    rmlist = SimpleXmlParser::getTagsProperties(ts4, "pippo");
    qDebug() << "Result0: " << rmlist.at(0);
    Q_ASSERT(rmlist.at(0).contains("p1"));
    Q_ASSERT(rmlist.at(0)["p1"]=="bello 'sguardo' ");
    Q_ASSERT(rmlist.at(0).contains("p2"));
    Q_ASSERT(rmlist.at(0)["p2"]=="ciccio");
    qDebug() << "Result1: " << rmlist.at(1);
    Q_ASSERT(rmlist.at(1).contains("p1"));
    Q_ASSERT(rmlist.at(1)["p1"]=="bello 'sguardo' ");
    Q_ASSERT(rmlist.at(1).contains("p3"));
    Q_ASSERT(rmlist.at(1)["p3"]=="ciccio2");
    qDebug() << "Test 4 passed\n----------\n";

    rm = new QMultiMap<QString, QString>(SimpleXmlParser::getTagProperties(ts5, "trap"));
    rm2 = new QMultiMap<QString, QString>(SimpleXmlParser::getTagProperties(ts5, "body"));
    rm->unite(*rm2);
    qDebug() << "Result: " << *rm;
    Q_ASSERT(rm->contains("eventType"));
    Q_ASSERT(rm->value("eventType")=="userInput");
    Q_ASSERT(rm->contains("networkType"));
    Q_ASSERT(rm->value("networkType")=="eth");
    Q_ASSERT(rm->contains("eventName"));
    Q_ASSERT(rm->value("eventName")=="PLAY");
    qDebug() << "Test 5 passed\n----------\n";
    delete rm2;
    delete rm;

    rm = new QMultiMap<QString, QString>(SimpleXmlParser::getTagProperties(ts6, "pippo"));
    qDebug() << "Result: " << *rm;
    Q_ASSERT(rm->contains("p1"));
    Q_ASSERT(decodeEntities(rm->value("p1"))=="alice < bob’s mom & '3 > 1'");
    Q_ASSERT(rm->contains("p2"));
    Q_ASSERT(decodeEntities(rm->value("p2"))=="éà€");
    qDebug() << "Test 6 passed\n----------\n";
    delete rm;
}

void
SimpleXmlParser::test_addData()
{
    /* Note: I'm using decodeEntities() to add unicode text to QString
     * irrespective of IDE/System encoding (GS)
     */
    QString tsPart1 = SimpleXmlParser::decodeEntities("<pippo>alice &lt; bob&#x2019;s mom &amp; '3 ");
    QString tsPart2 = SimpleXmlParser::decodeEntities("&gt; 1' &#233;&#224;&#8364;&#xc29f;</pippo>");

    SimpleXmlParser xmlParser;
    xmlParser.setStartTag("pippo");
    xmlParser.addData(tsPart1);
    xmlParser.addData(tsPart2);

    QString rs;
    if (xmlParser.hasPendingMessages())
    {
        rs = xmlParser.getNextMessage();
    }
    qDebug() << "Result: " << decodeEntities(rs);
    Q_ASSERT(rs == (tsPart1 + tsPart2));
    qDebug() << "Test 1 passed\n----------\n";
}

void
SimpleXmlParser::test_encodeEntities()
{
    // Named entities
    Q_ASSERT(encodeEntities("&")  == "&amp;");
    Q_ASSERT(encodeEntities("<")  == "&lt;");
    Q_ASSERT(encodeEntities(">")  == "&gt;");
    Q_ASSERT(encodeEntities("\"") == "&quot;");
    Q_ASSERT(encodeEntities("'")  == "&apos;");
    Q_ASSERT(encodeEntities("a&b<c>d\"e'f") == "a&amp;b&lt;c&gt;d&quot;e&apos;f");
    qDebug() << "Test encode named entities passed\n----------\n";

    // Non-ASCII: NOT encoded when encodeNonAscii=false (default)
    const QString eAccent = QString(QChar(0x00E9)); // é
    Q_ASSERT(encodeEntities(eAccent, false) == eAccent);
    qDebug() << "Test encode non-ASCII flag=false passed\n----------\n";

    // Non-ASCII: encoded when encodeNonAscii=true
    const QString encoded = encodeEntities(eAccent, true);
    Q_ASSERT(encoded == "&#xe9;");
    qDebug() << "Test encode non-ASCII flag=true passed\n----------\n";

    // Double-encoding bug fix: & inside &#xe9; must NOT be re-encoded to &amp;#xe9;
    Q_ASSERT(!encoded.contains("&amp;"));
    qDebug() << "Test no double-encoding passed\n----------\n";

    // Round-trip: decodeEntities(encodeEntities(s)) == s
    const QString original = QString("Alice & Bob < 3 > 1 ") + eAccent;
    Q_ASSERT(decodeEntities(encodeEntities(original, true)) == original);
    qDebug() << "Test encode/decode round-trip passed\n----------\n";
}

void
SimpleXmlParser::test_decodeEntitiesEdgeCases()
{
    // Empty string
    Q_ASSERT(decodeEntities("") == "");
    qDebug() << "Test decode empty string passed\n----------\n";

    // U+FFFD replacement character -> space
    Q_ASSERT(decodeEntities(QString(QChar(0xFFFD))) == " ");
    qDebug() << "Test decode U+FFFD -> space passed\n----------\n";

    // Unrecognized & kept as-is (& copied, remaining chars copied normally)
    Q_ASSERT(decodeEntities("&foo;")        == "&foo;");
    Q_ASSERT(decodeEntities("alone & here") == "alone & here");
    qDebug() << "Test decode unrecognized & kept as-is passed\n----------\n";

    // Numeric entity without closing ; -> kept as-is (condition pos < length fails)
    Q_ASSERT(decodeEntities("&#123")  == "&#123");
    Q_ASSERT(decodeEntities("&#x1a")  == "&#x1a");
    qDebug() << "Test decode numeric entity without ; passed\n----------\n";

    // Decimal and hex numeric entities
    Q_ASSERT(decodeEntities("&#233;") == QString(QChar(0x00E9)));
    Q_ASSERT(decodeEntities("&#xe9;") == QString(QChar(0x00E9)));
    Q_ASSERT(decodeEntities("&#xE9;") == QString(QChar(0x00E9))); // uppercase hex digits
    qDebug() << "Test decode numeric entities (decimal and hex) passed\n----------\n";

    // All named entities
    Q_ASSERT(decodeEntities("&amp;")  == "&");
    Q_ASSERT(decodeEntities("&lt;")   == "<");
    Q_ASSERT(decodeEntities("&gt;")   == ">");
    Q_ASSERT(decodeEntities("&quot;") == "\"");
    Q_ASSERT(decodeEntities("&apos;") == "'");
    qDebug() << "Test decode all named entities passed\n----------\n";
}

void
SimpleXmlParser::test_getDecodedTagHelpers()
{
    const QString msg = "<pippo>alice &lt; bob &amp; '3 &gt; 1'</pippo>";

    // getDecodedTagValue
    Q_ASSERT(getDecodedTagValue(msg, "pippo") == "alice < bob & '3 > 1'");
    qDebug() << "Test getDecodedTagValue passed\n----------\n";

    // getDecodedTagValue with default on missing tag
    Q_ASSERT(getDecodedTagValue(msg, "nonexistent", 0, "default") == "default");
    qDebug() << "Test getDecodedTagValue default passed\n----------\n";

    // getDecodedTagsValues
    const QString multi = "<list><item>a &amp; b</item><item>&lt;c&gt;</item></list>";
    const QStringList decodedList = getDecodedTagsValues(getTagValue(multi, "list"), "item");
    Q_ASSERT(decodedList.size() == 2);
    Q_ASSERT(decodedList.at(0) == "a & b");
    Q_ASSERT(decodedList.at(1) == "<c>");
    qDebug() << "Test getDecodedTagsValues passed\n----------\n";
}

void
SimpleXmlParser::test_addDataErrors()
{
    // Test E_MessageTooBig: second addData not appended when buffer already over limit
    {
        SimpleXmlParser parser;
        parser.setStartTag("msg");
        parser.setMaxBufferSize(10);
        // First add: buffer is empty (0 <= 10), data appended; no <msg> tag found
        parser.addData("12345678901"); // 11 chars, stays in buffer
        const int sizeBefore = parser.getCurrentBuffer().size();
        Q_ASSERT(sizeBefore > 10);
        // Second add: buffer.size() > 10 -> E_MessageTooBig emitted, return without appending
        parser.addData("more");
        Q_ASSERT(parser.getCurrentBuffer().size() == sizeBefore);
        qDebug() << "Test E_MessageTooBig: buffer not grown passed\n----------\n";
    }

    // Test stray close-tag before open-tag: the spurious </pippo> (idx2 < idx) is
    // reported as E_EndTagNotMatched and dropped; the leftover "garbage" before the
    // real <pippo> is then reported as E_UnexpectedData and dropped too; finally the
    // valid message is extracted correctly.
    {
        SimpleXmlParser parser;
        parser.setStartTag("pippo");
        QList<ParseErrorEnumType> errors;
        QObject::connect(&parser, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errors.append(e); });
        parser.addData("</pippo>garbage<pippo>valid content</pippo>");
        Q_ASSERT(errors.size() == 2);
        Q_ASSERT(errors.at(0) == SimpleXmlParser::E_EndTagNotMatched);
        Q_ASSERT(errors.at(1) == SimpleXmlParser::E_UnexpectedData);
        Q_ASSERT(parser.hasPendingMessages());
        Q_ASSERT(parser.getNextMessage() == "<pippo>valid content</pippo>");
        Q_ASSERT(!parser.hasPendingMessages());
        qDebug() << "Test stray close-tag + garbage: both errors emitted, valid message extracted passed\n----------\n";
    }

    // Test plain garbage before a start tag: E_UnexpectedData is emitted, the garbage
    // is dropped, and the valid message that follows is extracted correctly.
    {
        SimpleXmlParser parser;
        parser.setStartTag("pippo");
        bool errorFired = false;
        ParseErrorEnumType errorReceived = static_cast<ParseErrorEnumType>(-1);
        QObject::connect(&parser, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errorFired = true; errorReceived = e; });
        parser.addData("garbage<pippo>valid content</pippo>");
        Q_ASSERT(errorFired);
        Q_ASSERT(errorReceived == SimpleXmlParser::E_UnexpectedData);
        Q_ASSERT(parser.hasPendingMessages());
        Q_ASSERT(parser.getNextMessage() == "<pippo>valid content</pippo>");
        Q_ASSERT(!parser.hasPendingMessages());
        qDebug() << "Test garbage before start tag: E_UnexpectedData emitted, valid message extracted passed\n----------\n";
    }

    // Test whitespace/newlines between messages: this is legal and must NOT emit any
    // error. Both messages are extracted correctly.
    {
        SimpleXmlParser parser;
        parser.setStartTag("pippo");
        bool errorFired = false;
        QObject::connect(&parser, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType){ errorFired = true; });
        parser.addData("<pippo>a</pippo>\n   \t<pippo>b</pippo>");
        Q_ASSERT(!errorFired);
        Q_ASSERT(parser.getNextMessage() == "<pippo>a</pippo>");
        Q_ASSERT(parser.getNextMessage() == "<pippo>b</pippo>");
        Q_ASSERT(!parser.hasPendingMessages());
        qDebug() << "Test whitespace between messages: no error, both messages extracted passed\n----------\n";
    }

    // Test isolated close-tag with NO following start tag: E_EndTagNotMatched is
    // emitted and the stray close tag is dropped from the buffer.
    // A close tag with no matching start tag is always a parse error, whether or not
    // a start tag follows it in the same buffer.
    {
        SimpleXmlParser parser;
        parser.setStartTag("pippo");
        bool errorFired = false;
        QObject::connect(&parser, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType){ errorFired = true; });
        parser.addData("</pippo>garbage");
        Q_ASSERT(errorFired);
        Q_ASSERT(!parser.hasPendingMessages());
        Q_ASSERT(parser.getCurrentBuffer() == "garbage");
        qDebug() << "Test isolated close-tag without start tag: error emitted, stray tag dropped passed\n----------\n";
    }

    // Test many buffered messages in a single addData (verifies loop, not recursion)
    {
        SimpleXmlParser parser;
        parser.setStartTag("msg");
        QString bigBatch;
        for (int i = 0; i < 500; ++i) {
            bigBatch += QString("<msg>message %1</msg>").arg(i);
        }
        parser.addData(bigBatch);
        int count = 0;
        while (parser.hasPendingMessages()) {
            parser.getNextMessage();
            ++count;
        }
        Q_ASSERT(count == 500);
        qDebug() << "Test 500 messages in single addData (loop, no stack overflow) passed\n----------\n";
    }
}

void
SimpleXmlParser::test_bufferOps()
{
    SimpleXmlParser parser;
    parser.setStartTag("pippo");

    // Initial state
    Q_ASSERT(parser.getCurrentBuffer().isEmpty());
    Q_ASSERT(parser.getMaxBufferSize() == 0); // 0 = unlimited

    // Partial message stays in buffer (no close tag yet)
    parser.addData("<pippo>partial");
    Q_ASSERT(parser.getCurrentBuffer() == "<pippo>partial");

    // emptyBuffer clears it
    parser.emptyBuffer();
    Q_ASSERT(parser.getCurrentBuffer().isEmpty());

    // setMaxBufferSize with valid values
    parser.setMaxBufferSize(1024);
    Q_ASSERT(parser.getMaxBufferSize() == 1024);
    parser.setMaxBufferSize(0); // restore to unlimited
    Q_ASSERT(parser.getMaxBufferSize() == 0);

    // setMaxBufferSize with invalid value (-1) is ignored
    parser.setMaxBufferSize(512);
    parser.setMaxBufferSize(-1);
    Q_ASSERT(parser.getMaxBufferSize() == 512);

    qDebug() << "Test buffer ops passed\n----------\n";
}

void
SimpleXmlParser::test_signals()
{
    // --- E_NotifyOnly (default) ---
    // messageCompleted fires; parsedMessage and parseErrorFound do NOT fire.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        bool completedFired = false;
        bool parsedFired    = false;
        bool errorFired     = false;
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &){ parsedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType){ errorFired = true; });
        p.addData("<msg>hello</msg>");
        Q_ASSERT(completedFired);
        Q_ASSERT(!parsedFired);
        Q_ASSERT(!errorFired);
        Q_ASSERT(p.hasPendingMessages());
        Q_ASSERT(p.getNextMessage() == "<msg>hello</msg>");
        qDebug() << "Test signal E_NotifyOnly passed\n----------\n";
    }

    // --- E_DispatchMessage ---
    // parsedMessage fires with correct content; messageCompleted and parseErrorFound do NOT fire.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        p.setNotificationMode(SimpleXmlParser::E_DispatchMessage);
        bool completedFired = false;
        QString receivedMsg;
        bool errorFired = false;
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &m){ receivedMsg = m; });
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType){ errorFired = true; });
        p.addData("<msg>dispatch</msg>");
        Q_ASSERT(!completedFired);
        Q_ASSERT(receivedMsg == "<msg>dispatch</msg>");
        Q_ASSERT(!errorFired);
        Q_ASSERT(p.hasPendingMessages());
        qDebug() << "Test signal E_DispatchMessage passed\n----------\n";
    }

    // --- E_DispatchMessageAndDelete ---
    // parsedMessage fires with correct content; messageCompleted and parseErrorFound do NOT fire.
    // Message is NOT put in queue.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        p.setNotificationMode(SimpleXmlParser::E_DispatchMessageAndDelete);
        bool completedFired = false;
        QString receivedMsg;
        bool errorFired = false;
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &m){ receivedMsg = m; });
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType){ errorFired = true; });
        p.addData("<msg>delete</msg>");
        Q_ASSERT(!completedFired);
        Q_ASSERT(receivedMsg == "<msg>delete</msg>");
        Q_ASSERT(!errorFired);
        Q_ASSERT(!p.hasPendingMessages());
        qDebug() << "Test signal E_DispatchMessageAndDelete passed\n----------\n";
    }

    // --- E_NotifyAndDispatch ---
    // Both messageCompleted and parsedMessage fire; parseErrorFound does NOT fire.
    // Message is also in queue.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        p.setNotificationMode(SimpleXmlParser::E_NotifyAndDispatch);
        bool completedFired = false;
        QString receivedMsg;
        bool errorFired = false;
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &m){ receivedMsg = m; });
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType){ errorFired = true; });
        p.addData("<msg>both</msg>");
        Q_ASSERT(completedFired);
        Q_ASSERT(receivedMsg == "<msg>both</msg>");
        Q_ASSERT(!errorFired);
        Q_ASSERT(p.hasPendingMessages());
        Q_ASSERT(p.getNextMessage() == "<msg>both</msg>");
        qDebug() << "Test signal E_NotifyAndDispatch passed\n----------\n";
    }

    // --- parseErrorFound(E_MessageTooBig) ---
    // parseErrorFound fires with correct enum value; messageCompleted and parsedMessage do NOT fire.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        p.setMaxBufferSize(10);
        bool completedFired = false;
        bool parsedFired    = false;
        ParseErrorEnumType errorReceived = static_cast<ParseErrorEnumType>(-1);
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &){ parsedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errorReceived = e; });
        p.addData("12345678901"); // 11 chars, appended (check was false on empty buffer)
        p.addData("x");          // buffer.size() > 10 -> E_MessageTooBig, x rejected
        Q_ASSERT(errorReceived == SimpleXmlParser::E_MessageTooBig);
        Q_ASSERT(!completedFired);
        Q_ASSERT(!parsedFired);
        qDebug() << "Test signal parseErrorFound(E_MessageTooBig) passed\n----------\n";
    }

    // --- E_EndTagNotMatched emitted when a spurious close tag precedes the start tag ---
    // A stray </msg> before <msg> (idx2 < idx) is detected and E_EndTagNotMatched is
    // emitted; the stray tag is dropped and the valid message is still extracted correctly.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        ParseErrorEnumType errorReceived = static_cast<ParseErrorEnumType>(-1);
        bool completedFired = false;
        bool parsedFired    = false;
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errorReceived = e; });
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &){ parsedFired = true; });
        p.addData("</msg><msg>valid</msg>");
        Q_ASSERT(errorReceived == SimpleXmlParser::E_EndTagNotMatched);
        Q_ASSERT(completedFired);
        Q_ASSERT(!parsedFired);
        Q_ASSERT(p.hasPendingMessages());
        Q_ASSERT(p.getNextMessage() == "<msg>valid</msg>");
        qDebug() << "Test signal E_EndTagNotMatched emitted for spurious close tag passed\n----------\n";
    }

    // --- E_UnexpectedData emitted when plain garbage precedes the start tag ---
    // Data before <msg> that is not a close tag is unexpected: E_UnexpectedData is
    // emitted, the garbage is dropped, and the valid message is extracted correctly.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        ParseErrorEnumType errorReceived = static_cast<ParseErrorEnumType>(-1);
        bool completedFired = false;
        bool parsedFired    = false;
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errorReceived = e; });
        QObject::connect(&p, &SimpleXmlParser::messageCompleted,
                         [&]{ completedFired = true; });
        QObject::connect(&p, &SimpleXmlParser::parsedMessage,
                         [&](const QString &){ parsedFired = true; });
        p.addData("garbage<msg>valid</msg>");
        Q_ASSERT(errorReceived == SimpleXmlParser::E_UnexpectedData);
        Q_ASSERT(completedFired);
        Q_ASSERT(!parsedFired);
        Q_ASSERT(p.hasPendingMessages());
        Q_ASSERT(p.getNextMessage() == "<msg>valid</msg>");
        qDebug() << "Test signal E_UnexpectedData emitted for garbage before start tag passed\n----------\n";
    }

    // --- Both errors emitted, in order, for a stray close tag followed by garbage ---
    // "</msg>garbage<msg>valid</msg>" first drops the spurious </msg> (E_EndTagNotMatched),
    // then drops the leftover "garbage" before <msg> (E_UnexpectedData).
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        QList<ParseErrorEnumType> errors;
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errors.append(e); });
        p.addData("</msg>garbage<msg>valid</msg>");
        Q_ASSERT(errors.size() == 2);
        Q_ASSERT(errors.at(0) == SimpleXmlParser::E_EndTagNotMatched);
        Q_ASSERT(errors.at(1) == SimpleXmlParser::E_UnexpectedData);
        Q_ASSERT(p.hasPendingMessages());
        Q_ASSERT(p.getNextMessage() == "<msg>valid</msg>");
        qDebug() << "Test signal both errors (close tag + garbage) in order passed\n----------\n";
    }

    // --- Garbage BEFORE a spurious close tag: errors reported in positional order ---
    // "gar</msg><msg>x</msg>": the leading "gar" precedes the stray </msg>, so
    // E_UnexpectedData is emitted first, then E_EndTagNotMatched for the stray tag,
    // and finally the valid message is extracted.
    {
        SimpleXmlParser p;
        p.setStartTag("msg");
        QList<ParseErrorEnumType> errors;
        QObject::connect(&p, &SimpleXmlParser::parseErrorFound,
                         [&](ParseErrorEnumType e){ errors.append(e); });
        p.addData("gar</msg><msg>x</msg>");
        Q_ASSERT(errors.size() == 2);
        Q_ASSERT(errors.at(0) == SimpleXmlParser::E_UnexpectedData);
        Q_ASSERT(errors.at(1) == SimpleXmlParser::E_EndTagNotMatched);
        Q_ASSERT(p.hasPendingMessages());
        Q_ASSERT(p.getNextMessage() == "<msg>x</msg>");
        qDebug() << "Test signal garbage before spurious close tag (positional order) passed\n----------\n";
    }
}

/************* END OF TEST FNXS ************/

/*!

  */
QString
SimpleXmlParser::getNextMessage()
{
    QString s;

    muxMsgList.lock();
        s = m_parsedMessages.isEmpty() ? "" : m_parsedMessages.takeFirst();
    muxMsgList.unlock();

    return s;
}

void
SimpleXmlParser::addData(const QString &aMsgpart) {
    QString msg;

    if (m_maxBufferSizeInBytes > 0 && m_buffer.size() > m_maxBufferSizeInBytes) {
        emit parseErrorFound(E_MessageTooBig);
#ifdef SXML_DBG
        qWarning() << "Buffer size is: "<< m_buffer.size() << " we passed the limit, appending not done!";
#endif
        return;
    }

    m_buffer.append(aMsgpart);

    // Loop instead of recursion to avoid stack overflow with many buffered messages
    while (true) {
        int idx = m_buffer.indexOf(m_cachedStartTagOpen);
        int idx2 = m_buffer.indexOf(m_cachedStartTagClose);

        // Spurious close tag: a close tag appears with no start tag before it
        // (either there is no start tag at all, or the close tag precedes it).
        // Any NON-whitespace data before it is unexpected too, so report it first
        // (positional order), then report the stray close tag; finally drop
        // everything up to and including it and re-check what is left.
        if (idx2 >= 0 && (idx < 0 || idx2 < idx)) {
            if (hasNonWhitespaceBefore(m_buffer, idx2)) {
#ifdef SXML_DBG
                qCritical() << "SXML - Unexpected data before spurious END tag, dropping it:\n"
                            << m_buffer.left(idx2);
#endif
                emit parseErrorFound(E_UnexpectedData);
            }
            m_buffer = m_buffer.mid(idx2 + m_cachedStartTagClose.length());
#ifdef SXML_DBG
            qCritical() << "SXML - Spurious END tag with no matching START tag, dropping it.";
            qDebug() << "SXML - New buffer contents:\n" << m_buffer;
#endif
            emit parseErrorFound(E_EndTagNotMatched);
            continue;
        }

        if (idx < 0) {
            // No start tag at all, nothing to do
            return;
        }

        if (idx2 < 0) { //entire message data did not fit in the read chunk!
#ifdef SXML_DBG
            qDebug() << "############ SXML - Storing arrived data for next Chunk #############";
            qDebug() << "SXML - Current buffer is:\n" << m_buffer;
#endif
            return;
        }

        {//normal message
            // Any NON-whitespace data before the start tag is unexpected (it has no
            // start tag of its own, mirroring the spurious close-tag case). Report it
            // and drop it. Inter-message whitespace/newlines are legal and ignored.
            if (idx > 0 && hasNonWhitespaceBefore(m_buffer, idx)) {
#ifdef SXML_DBG
                qCritical() << "SXML - Unexpected data before START tag, dropping it:\n"
                            << m_buffer.left(idx);
#endif
                emit parseErrorFound(E_UnexpectedData);
            }
            msg = m_buffer.mid(idx, idx2 - idx + m_cachedStartTagClose.length());
            m_buffer = m_buffer.mid(idx2 + m_cachedStartTagClose.length());
#ifdef SXML_DBG
            qDebug() << "SXML - We got a message: " << msg;
            qDebug() << "SXML - Whats left in the buffer:\n" << m_buffer;
#endif

            //here we have a completed message;
            if (m_notifyMode == E_DispatchMessageAndDelete) {
                emit parsedMessage(msg);
            }
            else {
                muxMsgList.lock();
                    m_parsedMessages.append(msg);
                muxMsgList.unlock();

                switch(m_notifyMode) {
                    case E_NotifyOnly:
                        emit messageCompleted();
                        break;
                    case E_DispatchMessage:
                        emit parsedMessage(msg);
                        break;
                    case E_NotifyAndDispatch:
                        emit messageCompleted();
                        emit parsedMessage(msg);
                        break;
                    case E_DispatchMessageAndDelete:
                        //We cannot be here, added just to avoid compilation warning
                        break;
                }
            }

            //now if we still have something in the buffer we go for another check
            if (m_buffer.isEmpty()) {
                return;
            }
            // continue the while loop to process next message
        }
    }
}

bool
SimpleXmlParser::hasPendingMessages()
{
    bool retval = false;
    muxMsgList.lock();
        if (m_parsedMessages.count() > 0)
            retval = true;
    muxMsgList.unlock();
    return retval;
}

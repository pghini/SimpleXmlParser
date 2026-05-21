/********************************************************************************
 *   Copyright (C) 2012-2016 by NetResults S.r.l. ( http://www.netresults.it )  *
 *   Author(s):																	*
 *				Francesco Lamonica		<f.lamonica@netresults.it>				*
 ********************************************************************************/

#ifndef SIMPLEXMLPARSER_H
#define SIMPLEXMLPARSER_H

#include <QObject>
#include <QStringList>
#include <QMutex>

/*
 *  Uncomment below macro to enable xml parsing extra debug
 *  PLease note: this is really verbose, enable only when necessarly
 */
///#define SXML_DBG 1

class SimpleXmlParser : public QObject
{
    Q_OBJECT

    QString m_StartTag;
    QString m_cachedStartTagOpen;   // "<tag>"
    QString m_cachedStartTagClose;  // "</tag>"
    QStringList m_TagsToSignal, m_parsedMessages;
    int m_lastTagPos;
    QString m_buffer;
    QMutex muxMsgList;
    int m_maxBufferSizeInBytes; //0 means unlmited and is the default

public:
    explicit SimpleXmlParser(QObject *parent=0);

    enum notificationMode { E_NotifyOnly, E_DispatchMessage, E_DispatchMessageAndDelete, E_NotifyAndDispatch };
    enum ParseErrorEnumType { E_EndTagNotMatched, E_MessageTooBig };

    void setNotificationMode(const notificationMode aMode)      { m_notifyMode = aMode;         }
    void setStartTag(const QString &aTag);
    void addTagToFind(const QString &aTag)                      { m_TagsToSignal.append(aTag);  }
    void addData(const QString &aMsgpart);
    QString getNextMessage();
    bool hasPendingMessages();
    int  getMaxBufferSize() const                               { return m_maxBufferSizeInBytes;        }
    void setMaxBufferSize(int sizeInBytes);
    void emptyBuffer();
    QString getCurrentBuffer() const;

    static QString      getTagValue          (const QString &msg, const QString &tag, int beginidx=0, QString defaultValue="");
    static QString      getDecodedTagValue   (const QString &msg, const QString &tag, int beginidx=0, QString defaultValue="");

    static QStringList  getTagsValues        (const QString &msg, const QString &tag);
    static QStringList  getDecodedTagsValues (const QString &msg, const QString &tag);

    static QMap<QString, QString>           getTagProperties    (const QString &msg, const QString &tag, int beginidx=0);
    static QList<QMap<QString, QString> >   getTagsProperties   (const QString &msg, const QString &tag);

    /*!
     * @brief Decode XML entities.
     *   Convert &amp; &gt; &lt; &quot; &apos; &#...; &#x...; into UTF8 characters.
     */
    static QString decodeEntities(const QString &s);

    /*!
     * @brief Encode XML entities.
     *   Convert &, >, <, ", ' and non-ASCII characters (if required) to XML entities.
     */
    static QString encodeEntities(const QString &s, bool encodeNonAscii=false);

    /* TEST FUNCTIONS */
    static void test_getTag();
    static void test_getProperty();
    static void test_addData();
    static void test_encodeEntities();
    static void test_decodeEntitiesEdgeCases();
    static void test_getDecodedTagHelpers();
    static void test_addDataErrors();
    static void test_bufferOps();
    static void test_signals();

signals:
    //void foundTag(QString tag, QString value);
    void messageCompleted();
    void parsedMessage(QString msg);
    void parseErrorFound(ParseErrorEnumType);

protected:
    notificationMode m_notifyMode;
};

#endif // SIMPLEXMLPARSER_H

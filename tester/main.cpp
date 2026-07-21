#include <QCoreApplication>
#include <QFile>
#include <QDebug>

#include <nrparamparser.h>
#include <SimpleXmlParser.h>


void testBoundaryRegression(const QString &tp)
{
    // TC1: getTagsValues for "Test" must NOT match TestData, TestList, Tester
    QString boundarySection = SimpleXmlParser::getTagValue(tp, "BoundaryTests");
    QStringList testTags = SimpleXmlParser::getTagsValues(boundarySection, "Test");
    qDebug() << "TC1 - BoundaryTests: found" << testTags.size() << "Test tags";
    Q_ASSERT(testTags.size() == 3); // <Test>...</Test>, <Test attr>...</Test>, <Test/>
    Q_ASSERT(testTags.at(0) == "this is the actual Test tag");
    Q_ASSERT(testTags.at(1) == "Test with attribute");
    Q_ASSERT(testTags.at(2) == ""); // self-closing
    qDebug() << "TC1 passed - tag name boundary disambiguation OK\n----------\n";
}

void testStarPipeBoundary(const QString &tp)
{
    // TC2: getTagValue for "foo" must NOT match <foo*bar> or <foo|baz>
    QString section = SimpleXmlParser::getTagValue(tp, "StarPipeBoundaryTests");
    QString fooValue = SimpleXmlParser::getTagValue(section, "foo");
    qDebug() << "TC2 - StarPipeBoundary: foo value =" << fooValue;
    Q_ASSERT(fooValue == "correct foo tag");

    QStringList fooValues = SimpleXmlParser::getTagsValues(section, "foo");
    qDebug() << "TC2 - found" << fooValues.size() << "foo tags";
    Q_ASSERT(fooValues.size() == 3); // <foo>correct</foo>, <foo attr='x'>...</foo>, <foo/>
    Q_ASSERT(fooValues.at(0) == "correct foo tag");
    Q_ASSERT(fooValues.at(1) == "foo with space boundary");
    Q_ASSERT(fooValues.at(2) == ""); // self-closing
    qDebug() << "TC2 passed - '*' and '|' are NOT valid tag boundaries\n----------\n";
}

void testMultipleDoubleQuotedProperties(const QString &tp)
{
    // TC3: multiple double-quoted properties parsed correctly
    QString section = SimpleXmlParser::getTagValue(tp, "PropertyTests");
    QList<QMap<QString, QString> > propsList = SimpleXmlParser::getTagsProperties(section, "item");

    qDebug() << "TC3 - PropertyTests: found" << propsList.size() << "item tags";
    Q_ASSERT(propsList.size() == 4);

    // First item: p1="alpha" p2="beta"
    Q_ASSERT(propsList.at(0)["p1"] == "alpha");
    Q_ASSERT(propsList.at(0)["p2"] == "beta");

    // Second item: three props
    Q_ASSERT(propsList.at(1)["p1"] == "hello world");
    Q_ASSERT(propsList.at(1)["p2"] == "foo bar");
    Q_ASSERT(propsList.at(1)["p3"] == "baz");

    // Third: mixed quotes
    Q_ASSERT(propsList.at(2)["mixed"] == "double");
    Q_ASSERT(propsList.at(2)["single"] == "single");

    // Fourth: embedded single quotes inside double-quoted value
    Q_ASSERT(propsList.at(3)["p1"] == "value with 'inner single' quotes");
    Q_ASSERT(propsList.at(3)["p2"] == "another");

    qDebug() << "TC3 passed - multiple double-quoted properties OK\n----------\n";
}

void testDashPrefixedProperties(const QString &tp)
{
    // TC4: property names starting with dash
    QString section = SimpleXmlParser::getTagValue(tp, "DashPropertyTests");
    QMap<QString, QString> widgetProps = SimpleXmlParser::getTagProperties(section, "widget");
    qDebug() << "TC4 - DashPropertyTests: widget props =" << widgetProps;
    Q_ASSERT(widgetProps.contains("-data-id"));
    Q_ASSERT(widgetProps["-data-id"] == "123");
    Q_ASSERT(widgetProps.contains("-data-type"));
    Q_ASSERT(widgetProps["-data-type"] == "button");

    QMap<QString, QString> nodeProps = SimpleXmlParser::getTagProperties(section, "node");
    Q_ASSERT(nodeProps["data-value"] == "abc");
    Q_ASSERT(nodeProps["data-index"] == "0");

    qDebug() << "TC4 passed - dash-prefixed property names OK\n----------\n";
}

void testEmptyTagMixed(const QString &tp)
{
    // TC5: self-closing vs regular tags
    QString section = SimpleXmlParser::getTagValue(tp, "EmptyTagTests");
    QStringList entries = SimpleXmlParser::getTagsValues(section, "entry");
    qDebug() << "TC5 - EmptyTagTests: found" << entries.size() << "entry tags";
    Q_ASSERT(entries.size() == 6);
    Q_ASSERT(entries.at(0) == ""); // <entry/>
    Q_ASSERT(entries.at(1) == ""); // <entry />
    Q_ASSERT(entries.at(2) == ""); // <entry   />
    Q_ASSERT(entries.at(3) == ""); // <entry attr="x"/>
    Q_ASSERT(entries.at(4) == ""); // <entry attr="y" />
    Q_ASSERT(entries.at(5) == "has content");
    qDebug() << "TC5 passed - empty/self-closing tag detection OK\n----------\n";
}


int main(int argc, char** argv) {

    NRParamParser pp = NRParamParser::instance();
    pp.parse(argc,argv);
    QCoreApplication app(argc,argv);
    SimpleXmlParser xml;

    QFile f("testplan_76.xml");
    f.open(QIODevice::ReadOnly);
    QString tp = f.readAll();
    f.close();

    xml.setStartTag("TestPlan");
    xml.addData(tp);

    QString s = xml.getNextMessage();
    QStringList sl = xml.getTagsValues(s,"TestData");

    foreach (QString ts, sl) {
        qDebug() << "parsed: " << ts;
    }

    // --- Smoke test assertions ---

    // 1 self-closing <TestData/> at top level + 6 nested in the two phases
    Q_ASSERT(sl.size() == 7);
    Q_ASSERT(sl.at(0).isEmpty()); // <TestData/> self-closing
    qDebug() << "Smoke: TestData count (7) and self-closing OK";

    // Nested extraction from the first real TestData (index 1)
    Q_ASSERT(SimpleXmlParser::getTagValue(sl.at(1), "TestID") == "1");
    Q_ASSERT(SimpleXmlParser::getTagValue(sl.at(1), "Duration") == "60");
    qDebug() << "Smoke: nested TestID/Duration extraction OK";

    // Verify TestIDs across both phases: 1,2,3 repeated twice
    for (int i = 0; i < 2; ++i) {
        for (int j = 1; j <= 3; ++j) {
            Q_ASSERT(SimpleXmlParser::getTagValue(sl.at(i * 3 + j), "TestID") == QString::number(j));
        }
    }
    qDebug() << "Smoke: all TestID values (1,2,3 x2) OK";

    // Simple top-level values
    Q_ASSERT(SimpleXmlParser::getTagValue(s, "TPID") == "76");
    Q_ASSERT(SimpleXmlParser::getTagValue(s, "VlanId") == "1");
    Q_ASSERT(SimpleXmlParser::getTagValue(s, "RepeatMode") == "0");
    qDebug() << "Smoke: TPID/VlanId/RepeatMode OK";

    // Phase attribute extraction: two phases with phid="1" and phid="2"
    QString phaseList = SimpleXmlParser::getTagValue(s, "PhaseList");
    QList<QMap<QString, QString> > phases = SimpleXmlParser::getTagsProperties(phaseList, "Phase");
    Q_ASSERT(phases.size() == 2);
    Q_ASSERT(phases.at(0)["phid"] == "1");
    Q_ASSERT(phases.at(1)["phid"] == "2");
    qDebug() << "Smoke: Phase phid attributes OK";

    qDebug() << "========== SMOKE TEST PASSED ==========\n";

    // Original tests
    SimpleXmlParser::test_getTag();
    SimpleXmlParser::test_getProperty();
    SimpleXmlParser::test_addData();
    SimpleXmlParser::test_encodeEntities();
    SimpleXmlParser::test_decodeEntitiesEdgeCases();
    SimpleXmlParser::test_getDecodedTagHelpers();
    SimpleXmlParser::test_addDataErrors();
    SimpleXmlParser::test_bufferOps();
    SimpleXmlParser::test_signals();

    // Regression tests — loaded from dedicated fixture file
    QFile freg("regression_tests.xml");
    freg.open(QIODevice::ReadOnly);
    SimpleXmlParser xmlReg;
    xmlReg.setStartTag("RegressionTests");
    xmlReg.addData(freg.readAll());
    freg.close();
    QString reg = xmlReg.getNextMessage();

    qDebug() << "\n========== REGRESSION TESTS ==========\n";
    testBoundaryRegression(reg);
    testStarPipeBoundary(reg);
    testMultipleDoubleQuotedProperties(reg);
    testDashPrefixedProperties(reg);
    testEmptyTagMixed(reg);
    qDebug() << "\n========== ALL REGRESSION TESTS PASSED ==========\n";

    // Tests are synchronous: no event loop is required here.
    return 0;
}

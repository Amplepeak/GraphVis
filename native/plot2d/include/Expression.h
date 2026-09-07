#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace graphvis {

// A small expression evaluator, for the Function and Implicit engines.
//
// Those catalogue entries plot a formula rather than a dataset - y = f(x),
// z = f(x,y), or the zero set of f - so something has to turn typed text into
// numbers. This is that: a tokeniser, a shunting-yard parse into RPN, and a
// stack evaluator.
//
// Compiled once and evaluated many times, because a surface is a grid of tens
// of thousands of points and re-parsing the string at each one would make the
// engine unusable on anything but a coarse mesh.
//
// It is deliberately not a general scripting language. There is no assignment,
// no control flow and no way to reach outside the expression: the only things
// an expression can name are its variables and the functions listed below.
class Expression {
public:
    // Returns false and fills error() when the text will not parse.
    bool compile(const QString& text, const QStringList& variables);
    bool isValid() const { return valid_; }
    QString error() const { return error_; }
    QString source() const { return source_; }

    // Values are supplied positionally, in the order the variables were given
    // to compile(). Returns NaN for a domain error rather than throwing, so a
    // plot of tan(x) draws the branches it has instead of failing outright.
    double evaluate(const QVector<double>& values) const;

    // Functions this understands, for anything that wants to say so in the UI.
    static QStringList knownFunctions();

private:
    struct Token {
        enum Kind { Number, Variable, Operator, Function } kind;
        double value = 0.0;
        int index = 0;          // variable slot, or function id
        char op = 0;
    };
    bool valid_ = false;
    QString error_;
    QString source_;
    QVector<Token> rpn_;
};

} // namespace graphvis

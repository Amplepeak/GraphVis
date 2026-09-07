#include "Expression.h"

#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace graphvis {
namespace {

// Function ids. Kept as an enum rather than a string lookup at evaluation time,
// because evaluate() runs once per grid point and a hash lookup there is the
// difference between a surface that draws and one that stutters.
enum FnId {
    Fn_sin, Fn_cos, Fn_tan, Fn_asin, Fn_acos, Fn_atan, Fn_sinh, Fn_cosh, Fn_tanh,
    Fn_exp, Fn_log, Fn_log10, Fn_log2, Fn_sqrt, Fn_cbrt, Fn_abs, Fn_sign,
    Fn_floor, Fn_ceil, Fn_round, Fn_atan2, Fn_pow, Fn_min, Fn_max, Fn_hypot,
    Fn_Count
};

struct FnInfo { const char* name; int arity; };

const FnInfo kFunctions[Fn_Count]={
    {"sin",1},{"cos",1},{"tan",1},{"asin",1},{"acos",1},{"atan",1},
    {"sinh",1},{"cosh",1},{"tanh",1},{"exp",1},{"log",1},{"log10",1},{"log2",1},
    {"sqrt",1},{"cbrt",1},{"abs",1},{"sign",1},{"floor",1},{"ceil",1},{"round",1},
    {"atan2",2},{"pow",2},{"min",2},{"max",2},{"hypot",2},
};

int precedenceOf(char op){
    switch(op){
    case '+': case '-': return 1;
    case '*': case '/': case '%': return 2;
    case '^': return 3;
    // Unary minus sits in the SAME band as '^', not above it. At 4 it bound
    // tighter, so -x^2 tokenised to `x u 2 ^` = (-x)^2 and drew an upward
    // parabola with no error. Every other tool - including the matplotlib
    // these figures export to - reads -x^2 as -(x^2). isRightAssociative('u')
    // is already true, so equal precedence stops the shunting-yard popping it.
    case 'u': return 3;      // unary minus
    default: return 0;
    }
}

bool isRightAssociative(char op){ return op=='^'||op=='u'; }

double applyFunction(int id,double a,double b){
    switch(id){
    case Fn_sin: return std::sin(a);
    case Fn_cos: return std::cos(a);
    case Fn_tan: return std::tan(a);
    case Fn_asin: return std::asin(a);
    case Fn_acos: return std::acos(a);
    case Fn_atan: return std::atan(a);
    case Fn_sinh: return std::sinh(a);
    case Fn_cosh: return std::cosh(a);
    case Fn_tanh: return std::tanh(a);
    case Fn_exp: return std::exp(a);
    case Fn_log: return std::log(a);
    case Fn_log10: return std::log10(a);
    case Fn_log2: return std::log2(a);
    case Fn_sqrt: return std::sqrt(a);
    case Fn_cbrt: return std::cbrt(a);
    case Fn_abs: return std::abs(a);
    case Fn_sign: return (a>0)-(a<0);
    case Fn_floor: return std::floor(a);
    case Fn_ceil: return std::ceil(a);
    case Fn_round: return std::round(a);
    case Fn_atan2: return std::atan2(a,b);
    case Fn_pow: return std::pow(a,b);
    case Fn_min: return qMin(a,b);
    case Fn_max: return qMax(a,b);
    case Fn_hypot: return std::hypot(a,b);
    default: return std::numeric_limits<double>::quiet_NaN();
    }
}

} // namespace

QStringList Expression::knownFunctions(){
    QStringList out;
    for(int i=0;i<Fn_Count;++i) out<<QString::fromLatin1(kFunctions[i].name);
    return out;
}

bool Expression::compile(const QString& text,const QStringList& variables){
    rpn_.clear();
    valid_=false;
    error_.clear();
    source_=text;

    const QString expr=text.simplified();
    if(expr.isEmpty()){ error_=QStringLiteral("The formula is empty"); return false; }

    QVector<Token> output;
    QVector<Token> stack;
    // Tracks whether the next '-' is subtraction or a sign. Without this,
    // "-x" and "2*-x" both fail, and both are things people type.
    bool expectValue=true;

    int i=0;
    const int n=expr.size();
    while(i<n){
        const QChar ch=expr.at(i);
        if(ch.isSpace()){ ++i; continue; }

        if(ch.isDigit()||(ch==QLatin1Char('.')&&i+1<n&&expr.at(i+1).isDigit())){
            int j=i;
            while(j<n&&(expr.at(j).isDigit()||expr.at(j)==QLatin1Char('.'))) ++j;
            // Exponent form: 1e-3, 2.5E6.
            if(j<n&&(expr.at(j)==QLatin1Char('e')||expr.at(j)==QLatin1Char('E'))){
                int k=j+1;
                if(k<n&&(expr.at(k)==QLatin1Char('+')||expr.at(k)==QLatin1Char('-'))) ++k;
                if(k<n&&expr.at(k).isDigit()){
                    while(k<n&&expr.at(k).isDigit()) ++k;
                    j=k;
                }
            }
            bool ok=false;
            const double value=expr.mid(i,j-i).toDouble(&ok);
            if(!ok){ error_=QStringLiteral("Not a number: %1").arg(expr.mid(i,j-i)); return false; }
            output.append({Token::Number,value,0,0});
            expectValue=false;
            i=j;
            continue;
        }

        if(ch.isLetter()||ch==QLatin1Char('_')){
            int j=i;
            while(j<n&&(expr.at(j).isLetterOrNumber()||expr.at(j)==QLatin1Char('_'))) ++j;
            const QString word=expr.mid(i,j-i);
            i=j;

            const int varIndex=variables.indexOf(word);
            if(varIndex>=0){
                output.append({Token::Variable,0.0,varIndex,0});
                expectValue=false;
                continue;
            }
            if(word.compare(QLatin1String("pi"),Qt::CaseInsensitive)==0){
                output.append({Token::Number,3.14159265358979323846,0,0});
                expectValue=false;
                continue;
            }
            if(word==QLatin1String("e")){
                output.append({Token::Number,2.718281828459045,0,0});
                expectValue=false;
                continue;
            }
            int fn=-1;
            for(int k=0;k<Fn_Count;++k)
                if(word==QLatin1String(kFunctions[k].name)){ fn=k; break; }
            if(fn<0){
                error_=QStringLiteral("Unknown name \"%1\". Variables here are %2.")
                           .arg(word,variables.join(QStringLiteral(", ")));
                return false;
            }
            stack.append({Token::Function,0.0,fn,0});
            expectValue=true;
            continue;
        }

        if(ch==QLatin1Char('(')){
            stack.append({Token::Operator,0.0,0,'('});
            expectValue=true;
            ++i;
            continue;
        }
        if(ch==QLatin1Char(')')){
            bool matched=false;
            while(!stack.isEmpty()){
                const Token top=stack.last();
                if(top.kind==Token::Operator&&top.op=='('){ stack.removeLast(); matched=true; break; }
                output.append(top);
                stack.removeLast();
            }
            if(!matched){ error_=QStringLiteral("Unbalanced brackets"); return false; }
            if(!stack.isEmpty()&&stack.last().kind==Token::Function){
                output.append(stack.last());
                stack.removeLast();
            }
            expectValue=false;
            ++i;
            continue;
        }
        if(ch==QLatin1Char(',')){
            while(!stack.isEmpty()&&!(stack.last().kind==Token::Operator&&stack.last().op=='(')){
                output.append(stack.last());
                stack.removeLast();
            }
            expectValue=true;
            ++i;
            continue;
        }

        char op=ch.toLatin1();
        if(op=='+'||op=='-'||op=='*'||op=='/'||op=='^'||op=='%'){
            if((op=='-'||op=='+')&&expectValue) op=(op=='-')?'u':'p';
            if(op=='p'){ ++i; continue; }        // unary plus is a no-op
            while(!stack.isEmpty()){
                const Token top=stack.last();
                if(top.kind==Token::Function){ output.append(top); stack.removeLast(); continue; }
                if(top.kind!=Token::Operator||top.op=='(') break;
                const int a=precedenceOf(top.op), b=precedenceOf(op);
                if(a>b||(a==b&&!isRightAssociative(op))){ output.append(top); stack.removeLast(); }
                else break;
            }
            stack.append({Token::Operator,0.0,0,op});
            expectValue=true;
            ++i;
            continue;
        }

        error_=QStringLiteral("Unexpected character \"%1\"").arg(ch);
        return false;
    }

    while(!stack.isEmpty()){
        const Token top=stack.last();
        if(top.kind==Token::Operator&&top.op=='('){ error_=QStringLiteral("Unbalanced brackets"); return false; }
        output.append(top);
        stack.removeLast();
    }

    // A dry run catches arity mistakes now rather than at every grid point.
    int depth=0;
    for(const Token& t:output){
        switch(t.kind){
        case Token::Number: case Token::Variable: ++depth; break;
        case Token::Operator: depth-=(t.op=='u')?0:1; break;
        case Token::Function: depth-=(kFunctions[t.index].arity-1); break;
        }
        if(depth<1){ error_=QStringLiteral("The formula is incomplete"); return false; }
    }
    if(depth!=1){ error_=QStringLiteral("The formula has parts left over"); return false; }

    rpn_=output;
    valid_=true;
    return true;
}

double Expression::evaluate(const QVector<double>& values) const{
    if(!valid_) return std::numeric_limits<double>::quiet_NaN();
    // Fixed-size scratch: an expression deep enough to overflow this is not one
    // anybody is typing into a plot.
    double stack[64];
    int sp=0;
    for(const Token& t:rpn_){
        switch(t.kind){
        case Token::Number:
            if(sp>=64) return std::numeric_limits<double>::quiet_NaN();
            stack[sp++]=t.value;
            break;
        case Token::Variable:
            if(sp>=64) return std::numeric_limits<double>::quiet_NaN();
            stack[sp++]=values.value(t.index,std::numeric_limits<double>::quiet_NaN());
            break;
        case Token::Operator: {
            if(t.op=='u'){
                if(sp<1) return std::numeric_limits<double>::quiet_NaN();
                stack[sp-1]=-stack[sp-1];
                break;
            }
            if(sp<2) return std::numeric_limits<double>::quiet_NaN();
            const double b=stack[--sp];
            const double a=stack[--sp];
            double r=std::numeric_limits<double>::quiet_NaN();
            switch(t.op){
            case '+': r=a+b; break;
            case '-': r=a-b; break;
            case '*': r=a*b; break;
            // Division and modulo by zero give NaN, which the plot skips as a
            // gap - the honest picture of an undefined point.
            case '/': r=(std::abs(b)<1e-300)?std::numeric_limits<double>::quiet_NaN():a/b; break;
            case '%': r=(std::abs(b)<1e-300)?std::numeric_limits<double>::quiet_NaN():std::fmod(a,b); break;
            case '^': r=std::pow(a,b); break;
            default: break;
            }
            stack[sp++]=r;
            break;
        }
        case Token::Function: {
            const int arity=kFunctions[t.index].arity;
            if(sp<arity) return std::numeric_limits<double>::quiet_NaN();
            const double b=(arity==2)?stack[--sp]:0.0;
            const double a=stack[--sp];
            stack[sp++]=applyFunction(t.index,a,b);
            break;
        }
        }
    }
    return (sp==1)?stack[0]:std::numeric_limits<double>::quiet_NaN();
}

} // namespace graphvis

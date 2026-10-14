// modules/calculator.js
// Safe mathematical expression evaluator for Fly Browser address bar

function tokenize(expr) {
    if (!expr || typeof expr !== 'string') return null;
    let s = expr.trim().toLowerCase();

    // Reject URLs, protocols, commands, or workspace syntax
    if (s.startsWith('http://') || s.startsWith('https://') || s.startsWith('ws:') || s.startsWith('ws ') || s.startsWith('!')) {
        return null;
    }
    if (s.includes('://')) return null;

    // Normalise human-friendly arithmetic symbols
    s = s.replace(/×/g, '*').replace(/÷/g, '/');
    s = s.replace(/(\d)\s*[xX]\s*(\d)/g, '$1 * $2');
    s = s.replace(/(\d),(\d)/g, '$1$2');
    s = s.replace(/\s+of\s+/g, ' * ');
    s = s.replace(/([0-9.]+)%/g, '($1 / 100)');
    s = s.replace(/\*\*/g, '^');

    const tokens = [];
    let i = 0;
    while (i < s.length) {
        const c = s[i];
        if (/\s/.test(c)) {
            i++;
            continue;
        }
        if (/[0-9.]/.test(c)) {
            let numStr = '';
            while (i < s.length && /[0-9.]/.test(s[i])) {
                numStr += s[i++];
            }
            if (numStr === '.' || (numStr.match(/\./g) || []).length > 1) return null;
            tokens.push({ type: 'NUM', val: parseFloat(numStr) });
            continue;
        }
        if (/[a-z]/.test(c)) {
            let id = '';
            while (i < s.length && /[a-z0-9]/.test(s[i])) {
                id += s[i++];
            }
            tokens.push({ type: 'ID', val: id });
            continue;
        }
        if ('+-*/%^()'.includes(c)) {
            tokens.push({ type: 'OP', val: c });
            i++;
            continue;
        }
        return null;
    }
    return tokens;
}

export function evaluateMathExpression(expr) {
    const tokens = tokenize(expr);
    if (!tokens || tokens.length === 0) return null;

    let pos = 0;
    const peek = () => tokens[pos];
    const consume = () => tokens[pos++];

    // Must contain at least one operator or math function to be treated as a calculation
    const hasOp = tokens.some(t => t.type === 'OP' || (t.type === 'ID' && t.val !== 'pi' && t.val !== 'e'));
    if (!hasOp) return null;

    function parseExpression() {
        let node = parseTerm();
        while (peek() && (peek().val === '+' || peek().val === '-')) {
            const op = consume().val;
            const right = parseTerm();
            node = (op === '+') ? node + right : node - right;
        }
        return node;
    }

    function parseTerm() {
        let node = parsePower();
        while (peek() && (peek().val === '*' || peek().val === '/' || peek().val === '%')) {
            const op = consume().val;
            const right = parsePower();
            if (op === '*') {
                node = node * right;
            } else if (op === '/') {
                if (right === 0) throw new Error('Division by zero');
                node = node / right;
            } else if (op === '%') {
                node = node % right;
            }
        }
        return node;
    }

    function parsePower() {
        let node = parseFactor();
        if (peek() && peek().val === '^') {
            consume();
            const right = parsePower();
            node = Math.pow(node, right);
        }
        return node;
    }

    function parseFactor() {
        const token = peek();
        if (!token) throw new Error('Unexpected end of expression');

        if (token.type === 'OP' && (token.val === '-' || token.val === '+')) {
            consume();
            const factor = parseFactor();
            return token.val === '-' ? -factor : factor;
        }

        if (token.type === 'NUM') {
            consume();
            return token.val;
        }

        if (token.type === 'ID') {
            const id = consume().val;
            if (id === 'pi') return Math.PI;
            if (id === 'e') return Math.E;

            if (peek() && peek().val === '(') {
                consume();
                const arg = parseExpression();
                if (!peek() || peek().val !== ')') throw new Error('Expected closing parenthesis');
                consume();
                switch (id) {
                    case 'sqrt': return Math.sqrt(arg);
                    case 'cbrt': return Math.cbrt(arg);
                    case 'abs': return Math.abs(arg);
                    case 'sin': return Math.sin(arg);
                    case 'cos': return Math.cos(arg);
                    case 'tan': return Math.tan(arg);
                    case 'round': return Math.round(arg);
                    case 'floor': return Math.floor(arg);
                    case 'ceil': return Math.ceil(arg);
                    case 'log': return Math.log(arg);
                    case 'log2': return Math.log2(arg);
                    case 'log10': return Math.log10(arg);
                    default: throw new Error('Unknown function ' + id);
                }
            }
            throw new Error('Unknown identifier ' + id);
        }

        if (token.type === 'OP' && token.val === '(') {
            consume();
            const val = parseExpression();
            if (!peek() || peek().val !== ')') throw new Error('Expected closing parenthesis');
            consume();
            return val;
        }

        throw new Error('Unexpected token');
    }

    try {
        const result = parseExpression();
        if (pos !== tokens.length) return null;
        if (typeof result !== 'number' || isNaN(result) || !isFinite(result)) return null;

        // Clean floating-point precision quirks
        return parseFloat(result.toFixed(10));
    } catch (e) {
        return null;
    }
}

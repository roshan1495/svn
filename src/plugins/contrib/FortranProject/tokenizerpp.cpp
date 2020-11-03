#include "tokenizerpp.h"
#include "parserthreadf.h"

#include <wx/tokenzr.h>
#include <wx/filename.h>
#include <wx/filefn.h>


TokenizerPP::TokenizerPP(const wxString& filename, FortranSourceForm sourceForm, bool interpretPPDirectives)
{
    //ctor
    Tokenizerf* pFileTokens = new Tokenizerf(filename, sourceForm);
    m_TokensFiles.push_back(pFileTokens);
    m_ActiveFileIdx = 0;
    m_pParent = 0;
    m_interpretPPDirectives = interpretPPDirectives;
    m_PocketSize = 0;
    m_PocketWasPeeked = false;
    m_PeekedFromPocket = false;
    m_PocketLineNumber = 0;
}

TokenizerPP::~TokenizerPP()
{
    //dtor
    for (size_t i=0; i<m_TokensFiles.size(); ++i)
    {
        delete m_TokensFiles[i];
    }
}

void TokenizerPP::SetParent(ParserThreadF* parent)
{
    m_pParent = parent;
}

bool TokenizerPP::Init(const wxString& filename, FortranSourceForm sourceForm, bool interpretPPDirectives)
{
    m_ActiveFileIdx = 0;
    m_PocketSize = 0;
    m_interpretPPDirectives = interpretPPDirectives;
    return m_TokensFiles[0]->Init(filename, sourceForm);
}

bool TokenizerPP::InitFromBuffer(const wxString& buffer, FortranSourceForm sourceForm, bool interpretPPDirectives)
{
    m_ActiveFileIdx = 0;
    m_PocketSize = 0;
    m_interpretPPDirectives = interpretPPDirectives;
    return m_TokensFiles[0]->InitFromBuffer(buffer, sourceForm);
}

wxString TokenizerPP::GetToken()
{
    wxString token;
    if (m_PocketSize > 0)
    {
        // take token from the pocket
        if (m_PocketWasPeeked)
        {
            m_TokensFiles[m_ActiveFileIdx]->GetToken();
            m_PocketWasPeeked = false;
        }

        m_PocketSize -= 1;
        token = m_Pocket[m_PocketSize];
        m_Pocket.pop_back();

        return token;
    }

    while (true)
    {
        token = m_TokensFiles[m_ActiveFileIdx]->GetToken();

        if (m_interpretPPDirectives && token.GetChar(0) == '#')
        {
            if (token.IsSameAs(_T("#define")))
                HandlePPDefine();
            else if (token.IsSameAs(_T("#undefine")) || token.IsSameAs(_T("#undef")))
                HandlePPUndefine();
            else if (token.IsSameAs(_T("#if")) || token.IsSameAs(_T("#ifdef")) || token.IsSameAs(_T("#ifndef")))
                HandlePPIfdef(token);
            else if (token.IsSameAs(_T("#endif")) || token.IsSameAs(_T("#else")) || token.IsSameAs(_T("#elif")))
                HandlePPIfdef(token);
            else if (token.IsSameAs(_T("#include")))
                HandleInclude();
            else if (token.IsSameAs(_T("#error")))
                m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
            else
                break;  // don't know what it is
        }
        else if (token.IsEmpty())
        {
            // assume the end of file was reached
            if (m_ActiveFileIdx > 0)
            {
                delete m_TokensFiles[m_ActiveFileIdx];
                m_TokensFiles.pop_back();
                m_ActiveFileIdx = m_TokensFiles.size() - 1;
            }
            else
                return wxEmptyString; // no more tokens
        }
        else
            break;
    }

    token = ChangeWithDefinedValue(token);
    if (m_PocketSize > 0)
    {
        m_PocketWasPeeked = false;
        m_PocketLineNumber = m_TokensFiles[m_ActiveFileIdx]->GetLineNumber();
    }

    return token;
}

wxString TokenizerPP::InterpretDefinedFunction(const wxString& funName, const wxString& paramsIn)
{
    wxString value;
    if (!m_KnownFunctions.HasFunction(funName))
        return value; // sorry, function is unknown.

    if (!paramsIn.StartsWith(_T("(")) || !paramsIn.EndsWith(_T(")")))
        return value; // something is wrong

    wxString params = paramsIn.Mid(1, paramsIn.size()-2);

    wxString paramsChanged = _T("(");
    wxStringTokenizer tokenizer(params, _T(","));
    while (tokenizer.HasMoreTokens())
    {
        wxString par1 = tokenizer.GetNextToken();
        if (m_DefineTokens.count(par1) > 0)
            paramsChanged << m_DefineTokens[par1];
        else
            paramsChanged << par1;

        if (tokenizer.HasMoreTokens())
            paramsChanged << _T(",");
    }
    paramsChanged << _T(")");

    PreProcFunction* pFunc = m_KnownFunctions.GetFunction(funName);
    value = pFunc->Interpret(paramsChanged, &m_KnownFunctions);

    return value;
}

wxString TokenizerPP::ChangeWithDefinedValue(const wxString& token)
{
    if (token.StartsWith(_T("(")) && token.EndsWith(_T(")")))
    {
        // we have e.g. "(myType)"
        wxString newToken;
        wxString tokInside = token.Mid(1,token.size()-2).Trim(true).Trim(false);
        int idx1 = tokInside.Find('(');
        if (idx1 != wxNOT_FOUND)
        {
            // we have e.g. "CAT3(a,b,c)"
            wxString tok1 = tokInside.Mid(0, idx1);
            wxString tok2 = tokInside.Mid(idx1);

            if (m_DefineTokens.count(tok1) > 0)
            {
                wxString value = m_DefineTokens[tok1];
                if (value.StartsWith(_T("(")))
                {
                    // it is defined function
                    if (!m_KnownFunctions.HasFunction(tok1))
                        return token; // sorry, function is unknown.
                    else
                        return  (_T("(") + InterpretDefinedFunction(tok1, tok2) + _T(")"));
                }
                else
                    return (_T("(") + value + tok2 + _T(")"));
            }
        }
        else
        {
            if (m_DefineTokens.count(tokInside) > 0)
            {
                return (_T("(") + m_DefineTokens[tokInside] + _T(")"));
            }
        }
    }
    else if (m_DefineTokens.count(token) > 0)
    {
        return ChangeDefinedWithValue(token);
    }
    return token;
}

wxString TokenizerPP::ChangeDefinedWithValue(const wxString& token)
{
    if (m_DefineTokens[token].IsEmpty())
        return token; // this define has no value

    wxString value = m_DefineTokens[token];

    if (value.StartsWith(_T("(")))
    {
        // it is defined function
        if (!m_KnownFunctions.HasFunction(token))
            return token; // sorry, function is unknown.
        else
        {
            wxString peeked = m_TokensFiles[m_ActiveFileIdx]->PeekTokenSameFortranLine();
            if (!peeked.StartsWith(_T("(")))
                return token; // something is wrong
            wxString params = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameFortranLine();
            value = InterpretDefinedFunction(token, params);
            if (value.IsEmpty())
                return token; // something wrong with funciton interpretation
        }
    }


    value = CheckSaveInPocket(value);

    return value;
}

wxString TokenizerPP::CheckSaveInPocket(const wxString& token)
{
    //
    // 'token' can be e.g. type(myType). In this case, the function should return
    // only 'type' and '(myType)' should go to the pocket.
    // Similarly 'integer*4' should be split into 'integer', '*' and '4' tokens.
    //
    size_t pos1 = token.find('(');
    wxString value;
    wxString val_part1;
    wxString val_part2;
    wxString val_part3;
    if (pos1 != wxString::npos)
    {
        val_part1 = token.Mid(0, pos1).Trim();
        size_t pos2 = token.rfind(')');
        if (pos2 != wxString::npos)
        {
            val_part2 = token.Mid(pos1, pos2-pos1+1);
            val_part3 = token.Mid(pos2+1).Trim();
        }
        else
            val_part2 = token.Mid(pos1);
    }
    else
    {
        pos1 = token.find('*');
        if (pos1 == wxString::npos)
            return token;

        val_part1 = token.Mid(0, pos1).Trim();
        val_part2 = _T("*");
        val_part3 = token.Mid(pos1+1);
    }

    if (val_part1.IsEmpty())
    {
        val_part1 = val_part2;
        val_part2 = val_part3;
        val_part3 = wxEmptyString;
    }
    value = val_part1;
    if (!val_part3.IsEmpty())
        m_Pocket.push_back(val_part3);
    m_Pocket.push_back(val_part2);
    m_PocketSize = m_Pocket.size();

    return value;
}

void TokenizerPP::HandlePPDefine()
{
    // Handle #define ABC
    // or #define ABC abc
    //
    wxString token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
    if (token.IsEmpty())
        return; // something wrong
    wxString line = m_TokensFiles[m_ActiveFileIdx]->GetCurrentLine();
    size_t idx1 = line.find(token);
    if (idx1 == wxString::npos)
        return; // token not found. Something is wrong

    wxString value;
    wxString val_line = line.Mid(idx1+token.size()).Trim(true).Trim(false);
    bool isNewFunDefinition = val_line.StartsWith(_T("("));
    // Replace defined values in val_line
    if (isNewFunDefinition)
    {
        // I do not interpret (change) the line with function definition. Should I?
        value = val_line;
    }
    else if (!val_line.IsEmpty())
    {
        bool hasFunCall = false;
        size_t funNamePosShearch = 0;
        wxString funName;
        const wxString delimeters = _T(" ,()[]");
        wxStringTokenizer tokenizer(val_line, delimeters, wxTOKEN_STRTOK);
        if (tokenizer.HasMoreTokens())
        {
            size_t last_pos = 0;
            while (tokenizer.HasMoreTokens())
            {
                wxString word = tokenizer.GetNextToken();
                size_t pos = tokenizer.GetPosition();
                if (delimeters.find(val_line.GetChar(pos-1)) != wxString::npos)
                    pos -= 1;
                size_t pos_wstart = pos - word.size();
                if (m_DefineTokens.count(word) > 0)
                {
                    wxString def_val = m_DefineTokens[word];
                    if (def_val.StartsWith(_T("(")))
                    {
                        // It is call to a defined function.
                        if (m_KnownFunctions.HasFunction(word))
                        {
                            // Do nothing at first.
                            hasFunCall = true;
                            funNamePosShearch = value.length(); // after this position will come delimiters and then function name.
                            funName = word;
                        }
                        else
                            word << def_val; // unknown function.
                    }
                    else
                        word = def_val;
                }
                value << val_line.Mid(last_pos, pos_wstart-last_pos); // delimiter
                value << word;
                last_pos = pos;
                if (!tokenizer.HasMoreTokens())
                {
                    // add trailing delimeters
                    value << val_line.Mid(pos);
                }
            }
        }
        else
            value = val_line;

        if (hasFunCall)
        {
            // Function call.
            int funNamePos = funNamePosShearch + value.Mid(funNamePosShearch).Find(funName);
            size_t callStart = value.find('(', funNamePos);
            size_t callEnd = value.find(')', callStart+1);
            if (callStart != wxString::npos && callEnd != wxString::npos && callStart < callEnd)
            {
                wxString callArgs = value.Mid(callStart, callEnd-callStart+1);
                wxString word = InterpretDefinedFunction(funName, callArgs);
                value = value.Mid(0, funNamePos) + word + value.Mid(callEnd+1);
            }
        }
    }

    m_DefineTokens[token] = value; // add value even if it is empty
    m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();

    if (isNewFunDefinition)
    {
        // it is a function definition.
        size_t idxArgEnd = value.find(')');
        if (idxArgEnd != wxString::npos)
        {
            wxString argStr = value.Mid(0, idxArgEnd+1);
            wxString funBody = value.Mid(idxArgEnd+1).Trim(true).Trim(false);
            PreProcFunction* pFun = new PreProcFunction(token, argStr, funBody, &m_KnownFunctions);
            m_KnownFunctions.AddFunction(pFun);
        }
    }
}

void TokenizerPP::HandlePPUndefine()
{
    // Handle #undefine ABC or #undef ABC
    wxString token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
    if (token.IsEmpty())
        return; // something wrong
    m_DefineTokens.erase(token);
    m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
}

void TokenizerPP::HandlePPIfdef(const wxString& inToken, bool skipElif)
{
    wxString ifToken = inToken;
    // Handle #ifdef, #ifndef, #if, #if defined constructs.
    wxString ptok = PeekToken();

    if ( (ifToken.IsSameAs(_T("#if")) || (ifToken.IsSameAs(_T("#elif")) && !skipElif) )
        && ptok.IsSameAs(_T("defined")) )
    {
        GetToken(); // consume peeked token.
        ifToken = _T("#ifdef");
    }

    if ( ifToken.IsSameAs(_T("#ifdef")) || ifToken.IsSameAs(_T("#ifndef")) )
    {
        wxString token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
        if (token.IsEmpty())
            return; // something is wrong

        if (token.StartsWith('(') && token.EndsWith(')'))
        {
            token = token.Mid(1,token.Length()-2).Trim(true).Trim(false);
        }

// TODO (darmar#1#): Implementation does not consider case "#ifdef a || b".
        bool hasDef = (m_DefineTokens.count(token) > 0);

        if ((ifToken.IsSameAs(_T("#ifdef")) && hasDef) || (ifToken.IsSameAs(_T("#ifndef")) && !hasDef))
        {
            // Will be interpreted until corresponding #elif, #else or #endif
        }
        else
        {
            // Skip to the corresponding #elif #else or #endif
            m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
            wxString lastTok;
            SkipPPIfdef(lastTok);
            if (lastTok.IsSameAs(_T("#elif")))
                HandlePPIfdef(lastTok, false);
        }
    }
    else if (skipElif && (ifToken.IsSameAs(_T("#elif")) || ifToken.IsSameAs(_T("#else"))) )
    {
        wxString lastTok;
        while (true)
        {
            m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
            SkipPPIfdef(lastTok);
            if (lastTok.IsEmpty() || lastTok.IsSameAs(_T("#endif")) )
                break;
        }
    }
    else if (ifToken.IsSameAs(_T("#if")) || ifToken.IsSameAs(_T("#elif")))
    {
        // Handle:
        //    #if (PETSC_INT == 4)
        //    #if 0
        // How interpretation of #if/#elif can be improved?
        wxString token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
        if (token.IsEmpty())
            return; // something is wrong

        wxString condition = token;
        if (condition.StartsWith(_T("(")) && condition.EndsWith(_T(")")))
            condition = condition.Mid(1, condition.Length()-2).Trim(true).Trim(false);
        if (m_DefineTokens.count(token) > 0)
            condition = m_DefineTokens[token];
        bool answer = true;
        long lnum;
        if (condition.ToCLong(&lnum))
        {
            if (lnum == 0)
                answer = false;
            else
                answer = true;
        }
        else
        {
            // Handle:  "PETSC_INT == 4"
            wxArrayString conToks;
            MakeSaparateTokens(condition, conToks);
            if (conToks.size() == 3)
            {
                for (size_t i=0; i<3; i+=2)
                {
                    if (m_DefineTokens.count(conToks[i]) > 0)
                        conToks[i] = m_DefineTokens[conToks[i]];
                }

                long il;
                long ir;
                if (conToks[0].ToCLong(&il) && conToks[2].ToCLong(&ir))
                {
                    if (conToks[1] == _T("=="))
                    {
                        if (il == ir)
                            answer = true;
                        else
                            answer = false;
                    }
                    else if (conToks[1] == _T("!="))
                    {
                        if (il != ir)
                            answer = true;
                        else
                            answer = false;
                    }
                    else if (conToks[1] == _T(">"))
                    {
                        if (il > ir)
                            answer = true;
                        else
                            answer = false;
                    }
                    else if (conToks[1] == _T("<"))
                    {
                        if (il < ir)
                            answer = true;
                        else
                            answer = false;
                    }
                    else if (conToks[1] == _T(">="))
                    {
                        if (il >= ir)
                            answer = true;
                        else
                            answer = false;
                    }
                    else if (conToks[1] == _T("<="))
                    {
                        if (il <= ir)
                            answer = true;
                        else
                            answer = false;
                    }
                }
            }
        }

        if (answer)
        {
            // Will be interpreted until corresponding #elif, #else or #endif
        }
        else
        {
            // Skip to the corresponding #endif or #elif or #else
            wxString lastTok;
            while (true)
            {
                m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
                SkipPPIfdef(lastTok);
                if (lastTok.IsEmpty() || lastTok.IsSameAs(_T("#endif")) )
                {
                    break;
                }
                else if (lastTok.IsSameAs(_T("#elif")))
                {
                    HandlePPIfdef(lastTok, false);
                    break;
                }
                else if (lastTok.IsSameAs(_T("#else")))
                    break;
            }
        }
    }

    m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
}

void TokenizerPP::SkipPPIfdef(wxString& tokenAtEnd)
{
    // Skip to the next corresponding #elif, #else or #endif
    tokenAtEnd.clear();
    int inIfdef = 0;
    while (true)
    {
        wxString token = m_TokensFiles[m_ActiveFileIdx]->GetToken();
        if (token.IsEmpty())
            break;

        if (token.StartsWith(_T("#")))
        {
            if (token.IsSameAs(_T("#ifdef")) || token.IsSameAs(_T("#ifndef")) || token.IsSameAs(_T("#if")))
            {
                inIfdef += 1;
                m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
            }
            else if (inIfdef > 0 && token.IsSameAs(_T("#endif")))
            {
                inIfdef -= 1;
                m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
            }
            else if (token.IsSameAs(_T("#define")))
                continue;
            else if (token.IsSameAs(_T("#undefine")) || token.IsSameAs(_T("#undef")))
                continue;
            else if (inIfdef == 0 &&
                     (token.IsSameAs(_T("#elif")) || token.IsSameAs(_T("#else")) || token.IsSameAs(_T("#endif"))))
            {
                tokenAtEnd = token;
                break;
            }
        }
        else
            m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();
    }
}

void TokenizerPP::HandleInclude()
{
    wxString token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
    if (token.IsEmpty())
        return; // something wrong

    wxString includeFilename;
    if ( (token.StartsWith(_T("\"")) || token.StartsWith(_T("<"))) &&
         (token.EndsWith(_T("\""))  || token.EndsWith(_T(">"))) )
    {
        // Handle "name".
        includeFilename = token.Mid(1,token.size()-2).Trim(true).Trim(false);
    }
    else if (token.IsSameAs(_T("<")))
    {
        // Handle #include <filename.fpp>
        includeFilename = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
        if (m_TokensFiles[m_ActiveFileIdx]->PeekToken().IsSameAs(_T(".")))
        {
            wxString point = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();
            includeFilename.Append(point + m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine());
        }
    }
    m_TokensFiles[m_ActiveFileIdx]->SkipToEOL();

    // Check if the include file is in the same folder as an active file.
    wxFileName fileName(m_TokensFiles[m_ActiveFileIdx]->GetFilename(), wxPATH_UNIX);
    fileName.SetFullName(includeFilename);
    if (!wxFileExists(fileName.GetFullPath()))
    {
        // Include file not in the current file folder.
        // Check if this file is between additional directories for includes.
        if (!m_pParent)
            return;
        wxString aIncludeFile = m_pParent->GetAdditionalIncludeFile(includeFilename);
        if (aIncludeFile.IsEmpty())
            return;

        fileName.Assign(aIncludeFile, wxPATH_UNIX);
        if (!wxFileExists(fileName.GetFullPath()))
            return;
    }

    // Activate parsing of the include file.
    // SourceForm of file take the same as of the current file.
    Tokenizerf* pFileTokens = new Tokenizerf(fileName.GetFullPath(wxPATH_UNIX), m_TokensFiles[m_ActiveFileIdx]->GetSourceForm());
    m_TokensFiles.push_back(pFileTokens);
    m_ActiveFileIdx = m_TokensFiles.size() - 1;
}

wxString TokenizerPP::GetTokenSameLine()
{
    wxString token;
    if (m_PocketSize > 0)
    {
        // take token from the pocket
        m_PocketSize -= 1;
        token = m_Pocket[m_PocketSize];
        m_Pocket.pop_back();
        return token;
    }

    token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameLine();

    if (m_DefineTokens.count(token) > 0)
    {
        // token was defined with #define
        return ChangeDefinedWithValue(token);
    }

    return token;
}

wxString TokenizerPP::GetTokenSameFortranLine()
{
    wxString token;
    if (m_PocketSize > 0)
    {
        // take token from the pocket
        m_PocketSize -= 1;
        token = m_Pocket[m_PocketSize];
        m_Pocket.pop_back();
        return token;
    }

    token = m_TokensFiles[m_ActiveFileIdx]->GetTokenSameFortranLine();

    if (m_DefineTokens.count(token) > 0)
    {
        // token was defined with #define
        return ChangeDefinedWithValue(token);
    }

    return token;
}

wxString TokenizerPP::PeekToken()
{
    if (m_PocketSize > 0)
    {
        // take token from the pocket
        m_PeekedFromPocket = true;
        return m_Pocket[m_PocketSize-1];
    }

    wxString token = m_TokensFiles[m_ActiveFileIdx]->PeekToken();
    m_PeekedFromPocket = false;

    token = ChangeWithDefinedValue(token);
    if (m_PocketSize > 0)
    {
        m_Pocket.push_back(token);
        m_PocketSize = m_Pocket.size();
        m_PocketWasPeeked = true;
        m_PocketLineNumber = m_TokensFiles[m_ActiveFileIdx]->GetPeekedLineNumber();
    }
    return token;
}

wxString TokenizerPP::PeekTokenSameFortranLine()
{
    return m_TokensFiles[m_ActiveFileIdx]->PeekTokenSameFortranLine();
}

const wxString& TokenizerPP::GetParentFilename()
{
    return m_TokensFiles[0]->GetFilename();
}

unsigned int TokenizerPP::GetParentLineNumber()
{
    return m_TokensFiles[0]->GetLineNumber();
}

const wxString& TokenizerPP::GetFilename()
{
    return m_TokensFiles[m_ActiveFileIdx]->GetFilename();
}

unsigned int TokenizerPP::GetLineNumber()
{
    return m_TokensFiles[m_ActiveFileIdx]->GetLineNumber();
}

unsigned int TokenizerPP::GetPeekedLineNumber()
{
    if (m_PeekedFromPocket)
        return m_PocketLineNumber;

    return m_TokensFiles[m_ActiveFileIdx]->GetPeekedLineNumber();
}

unsigned int TokenizerPP::GetCurrentIndex()
{
    return m_TokensFiles[m_ActiveFileIdx]->GetCurrentIndex();
}

unsigned int TokenizerPP::GetLineCount()
{
    return m_TokensFiles[m_ActiveFileIdx]->GetLineCount();
}

bool TokenizerPP::IsOK()
{
    return m_TokensFiles[m_ActiveFileIdx]->IsOK();
}

bool TokenizerPP::SkipToOneOfChars(const char* chars, bool toLineEnd)
{
    return m_TokensFiles[m_ActiveFileIdx]->SkipToOneOfChars(chars, toLineEnd);
}

wxArrayString TokenizerPP::GetTokensToEOL(wxArrayString* arrStrLines)
{
    wxArrayString tokenArr;
    while (m_PocketSize > 0)
    {
        // take token from the pocket
        m_PocketSize -= 1;
        tokenArr.Add(m_Pocket[m_PocketSize]);
        m_Pocket.pop_back();
        if (arrStrLines)
            arrStrLines->Add(wxEmptyString);
    }

    wxArrayString* arrStrLinesEOL = NULL;
    if (arrStrLines)
        arrStrLinesEOL = new wxArrayString;
    wxArrayString tokenToEOL;
    m_TokensFiles[m_ActiveFileIdx]->GetTokensToEOL(tokenToEOL, arrStrLinesEOL);
    InterpretArrayString(tokenToEOL, tokenArr, arrStrLinesEOL, arrStrLines);
    if (arrStrLinesEOL)
        delete arrStrLinesEOL;

    return tokenArr;
}

void TokenizerPP::InterpretArrayString(const wxArrayString& tokenArrIn, wxArrayString& tokenArrOut,
                                       wxArrayString* arrStrLinesIn, wxArrayString* arrStrLinesOut)
{
    size_t arrSize = tokenArrIn.size();
    for (size_t i=0; i<arrSize; ++i)
    {
        if (m_DefineTokens.count(tokenArrIn[i]) > 0)
        {
            // token was defined with #define
            if (m_DefineTokens[tokenArrIn[i]].IsEmpty())
            {
                tokenArrOut.Add(tokenArrIn[i]); // no value is found. Add name of #define
            }
            else
            {
                 wxString value = m_DefineTokens[tokenArrIn[i]];
                if (value.StartsWith(_T("(")))
                {
                    // it is defined function
                    if (m_KnownFunctions.HasFunction(tokenArrIn[i]))
                    {
                        if ((i+1 < arrSize) && (tokenArrIn[i+1].StartsWith(_T("("))))
                        {
                            value = InterpretDefinedFunction(tokenArrIn[i], tokenArrIn[i+1]);
                        }
                    }
                }
                int pocketSize_old = m_PocketSize;
                value = CheckSaveInPocket(value);
                tokenArrOut.Add(value);
                if (arrStrLinesOut)
                    arrStrLinesOut->Add(arrStrLinesIn->Item(i));

                // new values can be added into the pocket in function CheckSaveInPocket.
                while (m_PocketSize > pocketSize_old)
                {
                    // take token from the pocket
                    m_PocketSize -= 1;
                    tokenArrOut.Add(m_Pocket[m_PocketSize]);
                    m_Pocket.pop_back();
                    if (arrStrLinesOut)
                        arrStrLinesOut->Add(wxEmptyString);
                }
            }
        }
        else
        {
            tokenArrOut.Add(tokenArrIn[i]);
            if (arrStrLinesOut)
                arrStrLinesOut->Add(arrStrLinesIn->Item(i));
        }
    }
}

wxArrayString TokenizerPP::PeekTokensToEOL()
{
    wxArrayString tokenArr;
    for (int i=m_PocketSize-1; i>=0; --i)
    {
        // take token from the pocket
        tokenArr.Add(m_Pocket[i]);
    }

    wxArrayString tokenToEOL = m_TokensFiles[m_ActiveFileIdx]->PeekTokensToEOL();
    InterpretArrayString(tokenToEOL, tokenArr, NULL, NULL);
    return tokenArr;
}

wxString TokenizerPP::GetCurrentLine()
{
    return m_TokensFiles[m_ActiveFileIdx]->GetCurrentLine();
}

wxString TokenizerPP::GetLineFortran()
{
    return m_TokensFiles[m_ActiveFileIdx]->GetLineFortran();
}

wxString TokenizerPP::GetLine(unsigned int nl)
{
    return m_TokensFiles[m_ActiveFileIdx]->GetLine(nl);
}

void TokenizerPP::MakeSaparateTokens(const wxString& line, wxArrayString tokenArr)
{
    Tokenizerf tokens;
    tokens.InitFromBuffer(line+_T(" "), fsfFree);
    while (true)
    {
        wxString token = tokens.GetToken();
        if (token.IsEmpty())
            break;

        tokenArr.Add(token.Lower());
    }
}


#ifndef TOKENIZERPP_H
#define TOKENIZERPP_H

#include <sdk.h>
#ifndef CB_PRECOMP
    #include <wx/string.h>
#endif
#include <vector>
#include <map>
#include <set>

#include "tokenizerf.h"
#include "preprocfunction.h"

class ParserThreadF;


/** \brief This class is a layer between Tokenizerf and ParserThreadF which interprets PreProcessor directives.
 */
class TokenizerPP
{
//    private:
//        struct TokenPP {
//            wxString m_DisplayName;
//            wxString m_Value;
//        };

    public:
        TokenizerPP(const wxString& filename = wxEmptyString, FortranSourceForm sourceForm = fsfFree,
                    bool interpretPPDirectives = true);
        ~TokenizerPP();

        bool Init(const wxString& filename, FortranSourceForm sourceForm, bool interpretPPDirectives);
		bool InitFromBuffer(const wxString& buffer, FortranSourceForm sourceForm, bool interpretPPDirectives);
		wxString GetToken();
		wxString GetTokenSameLine();
		wxString GetTokenSameFortranLine();
		wxString PeekToken();
		wxString PeekTokenSameFortranLine();
		bool IsInInclude() { return (m_ActiveFileIdx > 0); }
		const wxString& GetParentFilename();
        unsigned int GetParentLineNumber();
		const wxString& GetFilename();
		unsigned int GetLineNumber();
		unsigned int GetPeekedLineNumber();
		unsigned int GetCurrentIndex();
		unsigned int GetLineCount();

		bool IsOK();
		bool SkipToOneOfChars(const char* chars, bool toLineEnd = false);
		wxArrayString GetTokensToEOL(wxArrayString* arrStrLines = 0);
		wxArrayString PeekTokensToEOL();
		wxString GetCurrentLine();
		wxString GetLineFortran();
		wxString GetLine(unsigned int nl);

		unsigned int GetLineStartIndex(unsigned int indexInLine) { return m_TokensFiles[m_ActiveFileIdx]->GetLineStartIndex(indexInLine); }
		unsigned int GetLineEndIndex(unsigned int indexInLine) { return m_TokensFiles[m_ActiveFileIdx]->GetLineEndIndex(indexInLine); }
		void SetDetailedParsing(bool detPars) { return m_TokensFiles[m_ActiveFileIdx]->SetDetailedParsing(detPars); }
		void SetFilename(const wxString& filename) { return m_TokensFiles[m_ActiveFileIdx]->SetFilename(filename); }
		void SetParent(ParserThreadF* parent);
        void UngetToken() { return m_TokensFiles[m_ActiveFileIdx]->UngetToken(); }
		bool SkipToEOL() { return m_TokensFiles[m_ActiveFileIdx]->SkipToEOL(); }

    private:
        bool HandlePPMacro(const wxString& token);
        void HandlePPDefine();
        void HandlePPUndefine();
        void HandlePPIfdef(const wxString& inToken, bool skipElif=true);
        void SkipPPIfdef(wxString& tokenAtEnd);
        void HandleInclude();
        wxString InterpretDefinedFunction(const wxString& funName, const wxString& paramsIn);
        wxString ChangeWithDefinedValue(const wxString& token);
        wxString ChangeDefinedWithValue(const wxString& token);
        wxString CheckSaveInPocket(const wxString& token);
        void InterpretArrayString(const wxArrayString& tokenArrIn, wxArrayString& tokenArrOut,
                                       wxArrayString* arrStrLinesIn, wxArrayString* arrStrLinesOut);
        void MakeSaparateTokens(const wxString& line, wxArrayString tokenArr);

        ParserThreadF* m_pParent;
        bool m_interpretPPDirectives;
        std::map<wxString,wxString> m_DefineTokens;
        std::vector<Tokenizerf*> m_TokensFiles;  // every file has own Tokenizerf
        size_t m_ActiveFileIdx;
        PreProcFunctionList m_KnownFunctions;
        std::vector<wxString> m_Pocket;
        int m_PocketSize;
        bool m_PocketWasPeeked;
        bool m_PeekedFromPocket;
        unsigned int m_PocketLineNumber;
};

#endif // TOKENIZERPP_H

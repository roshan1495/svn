#ifndef CALLTREE_H
#define CALLTREE_H

#include <sdk.h>
#ifndef CB_PRECOMP
    #include <wx/stopwatch.h>
    #include <wx/progdlg.h>
#endif
#include <set>

#include "calltreeview.h"
#include "parserf.h"
class CallTree
{
    public:
        CallTree(FortranProject* forproj);
        virtual ~CallTree();

        CallTreeView* GetCallTreeView(){return m_pCallTreeView;};
        void BuildCallTree(cbEditor* ed, const wxString& NameUnderCursor, ParserF* pParser, std::set< wxString>& keywordSet, bool showCallTree);

    protected:

    private:
        void FindUsedModules(ParserF* pParser, CallTreeToken* token);
        void FindCalledTokens(ParserF* pParser, CallTreeToken* token, std::set< wxString>& keywordSet);
        void FindTokenFromCall(ParserF* pParser, TokenFlat* parentTok, TokenFlat* oneCall, TokensArrayFlat* result);
        bool HasChildToken(TokenF* tokParent, TokenF* tok);
        bool HasCallChildToken(TokenF* tokParent, TokenFlat* tok);
        bool HasInHerarchy(TokenF* tokParent, TokenF* tok);
        void ManageInterfaceExplicit(ParserF* pParser, TokenFlat* origFT, CallTreeToken* token, std::set<wxString>& keywordSet);
        void FindCallingTokens(ParserF* pParser, CallTreeToken* token, CalledByDict& cByDict);
        void ManageTBProceduresForCallTree(ParserF* pParser, TokenFlat* origFT, CallTreeToken* token, std::set<wxString>& keywordSet);

        CallTreeView* m_pCallTreeView;

        std::set<wxString> m_FortranIntrinsicModules;

        wxStopWatch m_StopWatch;
        long m_TimeOld;
        wxProgressDialog* m_pProgressDlg;
        bool m_Cancelled;
        int m_CallDepth;
        int m_CallDepthMax;
};

#endif // CALLTREE_H

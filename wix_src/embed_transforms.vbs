Option Explicit

' Embeds the English (1033) and Korean (1042) language transforms into the base
' MSI so the bundle's TRANSFORMS=:[WixStdBALanguageId] always resolves.
'
'   Usage: cscript //nologo embed_transforms.vbs <BaseMsi> <KoMsi> <TorchPath>
'
' The base MSI is English. The Korean transform carries the localized strings.
' torch refuses to emit an EMPTY transform (TRCH0227), and the base-vs-base
' English diff would be empty, so the 1033 transform is generated against a
' throwaway copy that holds one harmless dummy property (CUB_DUMMY_LANG_MARKER) -
' applying it leaves the English base unchanged. Both transforms are embedded in
' a SINGLE database session (multiple OpenDatabase/Commit cycles corrupted the
' storages and caused 0x80070658 on apply). The summary "Template" property is
' left exactly as light wrote it ("x64;1033") - overwriting it caused
' 0x80070661 (platform) / re-apply issues.

Const KO_LCID   = "1042"
Const EN_LCID   = "1033"
Const KO_MST    = "ko-kr.mst"
Const EN_MST    = "en-us.mst"
Const DUMMY_MSI = "CUBRID_Base_dummy.msi"
Const MSIOPEN_TRANSACT = 1

Dim sh, fso, args, baseMsi, koMsi, torchPath
Set sh  = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

Set args = WScript.Arguments
If args.Count < 3 Then
    WScript.Echo "[ERROR] usage: embed_transforms.vbs <BaseMsi> <KoMsi> <TorchPath>"
    WScript.Quit 2
End If
baseMsi   = args(0)
koMsi     = args(1)
torchPath = args(2)

' 1) Korean transform: base -> ko (localized strings)
RunTorch baseMsi, koMsi, KO_MST

' 2) English transform: base -> base + one harmless dummy property (non-empty)
If fso.FileExists(DUMMY_MSI) Then fso.DeleteFile DUMMY_MSI, True
fso.CopyFile baseMsi, DUMMY_MSI, True
AddDummyProperty DUMMY_MSI
RunTorch baseMsi, DUMMY_MSI, EN_MST

' 3) Embed both transforms into the base MSI in ONE database session
EmbedTransforms baseMsi

' 4) Clean up intermediates
DeleteIfExists DUMMY_MSI
DeleteIfExists KO_MST
DeleteIfExists EN_MST

WScript.Echo "[INFO] Embedded transforms " & EN_LCID & " (en-us) and " & KO_LCID & " (ko-kr)."
WScript.Quit 0

' -------------------------------------------------------------------- helpers

Sub RunTorch(fromMsi, toMsi, outMst)
    Dim cmd, rc
    cmd = """" & torchPath & """ -p -t language """ & fromMsi & """ """ & toMsi & """ -out """ & outMst & """"
    rc = sh.Run(cmd, 0, True)   ' 0 = hidden window, True = wait and return exit code
    If rc <> 0 Then
        WScript.Echo "[ERROR] torch failed (rc=" & rc & ") generating " & outMst
        WScript.Quit 1
    End If
End Sub

Sub AddDummyProperty(msi)
    Dim installer, db, view
    Set installer = CreateObject("WindowsInstaller.Installer")
    Set db = installer.OpenDatabase(msi, MSIOPEN_TRANSACT)
    Set view = db.OpenView("INSERT INTO `Property` (`Property`, `Value`) VALUES ('CUB_DUMMY_LANG_MARKER', '" & EN_LCID & "')")
    view.Execute
    db.Commit
    Set view = Nothing
    Set db = Nothing
End Sub

Sub EmbedTransforms(msi)
    Dim installer, db, view, rec
    Set installer = CreateObject("WindowsInstaller.Installer")
    Set db = installer.OpenDatabase(msi, MSIOPEN_TRANSACT)

    ' Korean transform -> storage "1042"
    Set view = db.OpenView("INSERT INTO `_Storages` (`Name`, `Data`) VALUES ('" & KO_LCID & "', ?)")
    Set rec = installer.CreateRecord(1)
    rec.SetStream 1, KO_MST
    view.Execute rec

    ' English transform -> storage "1033"
    Set view = db.OpenView("INSERT INTO `_Storages` (`Name`, `Data`) VALUES ('" & EN_LCID & "', ?)")
    Set rec = installer.CreateRecord(1)
    rec.SetStream 1, EN_MST
    view.Execute rec

    db.Commit   ' single commit; Template summary is intentionally left untouched
    Set view = Nothing
    Set rec = Nothing
    Set db = Nothing
End Sub

Sub DeleteIfExists(path)
    If fso.FileExists(path) Then fso.DeleteFile path, True
End Sub

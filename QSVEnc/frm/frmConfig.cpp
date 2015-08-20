﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

//以下warning C4100を黙らせる
//C4100 : 引数は関数の本体部で 1 度も参照されません。
#pragma warning( push )
#pragma warning( disable: 4100 )

#include "auo_version.h"
#include "auo_frm.h"
#include "auo_faw2aac.h"
#include "frmConfig.h"
#include "frmSaveNewStg.h"
#include "frmOtherSettings.h"
#include "frmBitrateCalculator.h"
#include "mfxstructures.h"

using namespace QSVEnc;

/// -------------------------------------------------
///     設定画面の表示
/// -------------------------------------------------
[STAThreadAttribute]
void ShowfrmConfig(CONF_GUIEX *conf, const SYSTEM_DATA *sys_dat) {
    if (!sys_dat->exstg->s_local.disable_visual_styles)
        System::Windows::Forms::Application::EnableVisualStyles();
    System::IO::Directory::SetCurrentDirectory(String(sys_dat->aviutl_dir).ToString());
    frmConfig frmConf(conf, sys_dat);
    frmConf.ShowDialog();
}

/// -------------------------------------------------
///     frmSaveNewStg 関数
/// -------------------------------------------------
System::Boolean frmSaveNewStg::checkStgFileName(String^ stgName) {
    String^ fileName;
    if (stgName->Length == 0)
        return false;
    
    if (!ValidiateFileName(stgName)) {
        MessageBox::Show(L"ファイル名に使用できない文字が含まれています。\n保存できません。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
        return false;
    }
    if (String::Compare(Path::GetExtension(stgName), L".stg", true))
        stgName += L".stg";
    if (File::Exists(fileName = Path::Combine(fsnCXFolderBrowser->GetSelectedFolder(), stgName)))
        if (MessageBox::Show(stgName + L" はすでに存在します。上書きしますか?", L"上書き確認", MessageBoxButtons::YesNo, MessageBoxIcon::Question)
            != System::Windows::Forms::DialogResult::Yes)
            return false;
    StgFileName = fileName;
    return true;
}

System::Void frmSaveNewStg::setStgDir(String^ _stgDir) {
    StgDir = _stgDir;
    fsnCXFolderBrowser->SetRootDirAndReload(StgDir);
}


/// -------------------------------------------------
///     frmBitrateCalculator 関数
/// -------------------------------------------------
System::Void frmBitrateCalculator::Init(int VideoBitrate, int AudioBitrate, bool BTVBEnable, bool BTABEnable, int ab_max) {
    guiEx_settings exStg(true);
    exStg.load_fbc();
    enable_events = false;
    fbcTXSize->Text = exStg.s_fbc.initial_size.ToString("F2");
    fbcChangeTimeSetMode(exStg.s_fbc.calc_time_from_frame != 0);
    fbcRBCalcRate->Checked = exStg.s_fbc.calc_bitrate != 0;
    fbcRBCalcSize->Checked = !fbcRBCalcRate->Checked;
    fbcTXMovieFrameRate->Text = Convert::ToString(exStg.s_fbc.last_fps);
    fbcNUMovieFrames->Value = exStg.s_fbc.last_frame_num;
    fbcNULengthHour->Value = Convert::ToDecimal((int)exStg.s_fbc.last_time_in_sec / 3600);
    fbcNULengthMin->Value = Convert::ToDecimal((int)(exStg.s_fbc.last_time_in_sec % 3600) / 60);
    fbcNULengthSec->Value =  Convert::ToDecimal((int)exStg.s_fbc.last_time_in_sec % 60);
    SetBTVBEnabled(BTVBEnable);
    SetBTABEnabled(BTABEnable, ab_max);
    SetNUVideoBitrate(VideoBitrate);
    SetNUAudioBitrate(AudioBitrate);
    enable_events = true;
}
System::Void frmBitrateCalculator::frmBitrateCalculator_FormClosing(System::Object^  sender, System::Windows::Forms::FormClosingEventArgs^  e) {
    guiEx_settings exStg(true);
    exStg.load_fbc();
    exStg.s_fbc.calc_bitrate = fbcRBCalcRate->Checked;
    exStg.s_fbc.calc_time_from_frame = fbcPNMovieFrames->Visible;
    exStg.s_fbc.last_fps = Convert::ToDouble(fbcTXMovieFrameRate->Text);
    exStg.s_fbc.last_frame_num = Convert::ToInt32(fbcNUMovieFrames->Value);
    exStg.s_fbc.last_time_in_sec = Convert::ToInt32(fbcNULengthHour->Value) * 3600
                                 + Convert::ToInt32(fbcNULengthMin->Value) * 60
                                 + Convert::ToInt32(fbcNULengthSec->Value);
    if (fbcRBCalcRate->Checked)
        exStg.s_fbc.initial_size = Convert::ToDouble(fbcTXSize->Text);
    exStg.save_fbc();
    frmConfig^ fcg = dynamic_cast<frmConfig^>(this->Owner);
    if (fcg != nullptr)
        fcg->InformfbcClosed();
}
System::Void frmBitrateCalculator::fbcRBCalcRate_CheckedChanged(System::Object^  sender, System::EventArgs^  e) {
    if (fbcRBCalcRate->Checked && Convert::ToDouble(fbcTXSize->Text) <= 0.0) {
        guiEx_settings exStg(true);
        exStg.load_fbc();
        fbcTXSize->Text = exStg.s_fbc.initial_size.ToString("F2");
    }
}
System::Void frmBitrateCalculator::fbcBTVBApply_Click(System::Object^  sender, System::EventArgs^  e) {
    frmConfig^ fcg = dynamic_cast<frmConfig^>(this->Owner);
    if (fcg != nullptr)
        fcg->SetVideoBitrate((int)fbcNUBitrateVideo->Value);
}
System::Void frmBitrateCalculator::fbcBTABApply_Click(System::Object^  sender, System::EventArgs^  e) {
    frmConfig^ fcg = dynamic_cast<frmConfig^>(this->Owner);
    if (fcg != nullptr)
        fcg->SetAudioBitrate((int)fbcNUBitrateAudio->Value);
}


/// -------------------------------------------------
///     frmConfig 関数  (frmBitrateCalculator関連)
/// -------------------------------------------------
System::Void frmConfig::CloseBitrateCalc() {
    frmBitrateCalculator::Instance::get()->Close();
}
System::Void frmConfig::fcgTSBBitrateCalc_CheckedChanged(System::Object^  sender, System::EventArgs^  e) {
    if (fcgTSBBitrateCalc->Checked) {
        bool videoBitrateMode = true;

        frmBitrateCalculator::Instance::get()->Init(
            (int)fcgNUBitrate->Value,
            (fcgNUAudioBitrate->Visible) ? (int)fcgNUAudioBitrate->Value : 0,
            videoBitrateMode,
            fcgNUAudioBitrate->Visible,
            (int)fcgNUAudioBitrate->Maximum
            );
        frmBitrateCalculator::Instance::get()->Owner = this;
        frmBitrateCalculator::Instance::get()->Show();
    } else {
        frmBitrateCalculator::Instance::get()->Close();
    }
}
System::Void frmConfig::SetfbcBTABEnable(bool enable, int max) {
    frmBitrateCalculator::Instance::get()->SetBTABEnabled(fcgNUAudioBitrate->Visible, max);
}
System::Void frmConfig::SetfbcBTVBEnable(bool enable) {
    frmBitrateCalculator::Instance::get()->SetBTVBEnabled(enable);
}

System::Void frmConfig::SetVideoBitrate(int bitrate) {
    SetNUValue(fcgNUBitrate, bitrate);
}

System::Void frmConfig::SetAudioBitrate(int bitrate) {
    SetNUValue(fcgNUAudioBitrate, bitrate);
}
System::Void frmConfig::InformfbcClosed() {
    fcgTSBBitrateCalc->Checked = false;
}


/// -------------------------------------------------
///     frmConfig 関数
/// -------------------------------------------------
/////////////   LocalStg関連  //////////////////////
System::Void frmConfig::LoadLocalStg() {
    guiEx_settings *_ex_stg = sys_dat->exstg;
    _ex_stg->load_encode_stg();
    LocalStg.CustomTmpDir    = String(_ex_stg->s_local.custom_tmp_dir).ToString();
    LocalStg.CustomAudTmpDir = String(_ex_stg->s_local.custom_audio_tmp_dir).ToString();
    LocalStg.CustomMP4TmpDir = String(_ex_stg->s_local.custom_mp4box_tmp_dir).ToString();
    LocalStg.LastAppDir      = String(_ex_stg->s_local.app_dir).ToString();
    LocalStg.LastBatDir      = String(_ex_stg->s_local.bat_dir).ToString();
    LocalStg.MP4MuxerExeName = String(_ex_stg->s_mux[MUXER_MP4].filename).ToString();
    LocalStg.MP4MuxerPath    = String(_ex_stg->s_mux[MUXER_MP4].fullpath).ToString();
    LocalStg.MKVMuxerExeName = String(_ex_stg->s_mux[MUXER_MKV].filename).ToString();
    LocalStg.MKVMuxerPath    = String(_ex_stg->s_mux[MUXER_MKV].fullpath).ToString();
    LocalStg.TC2MP4ExeName   = String(_ex_stg->s_mux[MUXER_TC2MP4].filename).ToString();
    LocalStg.TC2MP4Path      = String(_ex_stg->s_mux[MUXER_TC2MP4].fullpath).ToString();
    LocalStg.MPGMuxerExeName = String(_ex_stg->s_mux[MUXER_MPG].filename).ToString();
    LocalStg.MPGMuxerPath    = String(_ex_stg->s_mux[MUXER_MPG].fullpath).ToString();
    LocalStg.MP4RawExeName   = String(_ex_stg->s_mux[MUXER_MP4_RAW].filename).ToString();
    LocalStg.MP4RawPath      = String(_ex_stg->s_mux[MUXER_MP4_RAW].fullpath).ToString();

    LocalStg.audEncName->Clear();
    LocalStg.audEncExeName->Clear();
    LocalStg.audEncPath->Clear();
    for (int i = 0; i < _ex_stg->s_aud_count; i++) {
        LocalStg.audEncName->Add(String(_ex_stg->s_aud[i].dispname).ToString());
        LocalStg.audEncExeName->Add(String(_ex_stg->s_aud[i].filename).ToString());
        LocalStg.audEncPath->Add(String(_ex_stg->s_aud[i].fullpath).ToString());
    }
    if (_ex_stg->s_local.large_cmdbox)
        fcgTXCmd_DoubleClick(nullptr, nullptr); //初期状態は縮小なので、拡大
}

System::Boolean frmConfig::CheckLocalStg() {
    bool error = false;
    String^ err = "";
    //音声エンコーダのチェック (実行ファイル名がない場合はチェックしない)
    if (LocalStg.audEncExeName[fcgCXAudioEncoder->SelectedIndex]->Length) {
        String^ AudioEncoderPath = LocalStg.audEncPath[fcgCXAudioEncoder->SelectedIndex];
        if (!File::Exists(AudioEncoderPath) 
            && (fcgCXAudioEncoder->SelectedIndex != sys_dat->exstg->s_aud_faw_index 
                || !check_if_faw2aac_exists()) ) {
            //音声実行ファイルがない かつ
            //選択された音声がfawでない または fawであってもfaw2aacがない
            if (!error) err += L"\n\n";
            error = true;
            err += L"指定された 音声エンコーダ は存在しません。\n [ " + AudioEncoderPath + L" ]\n";
        }
    }
    //FAWのチェック
    if (fcgCBFAWCheck->Checked) {
        if (sys_dat->exstg->s_aud_faw_index == FAW_INDEX_ERROR) {
            if (!error) err += L"\n\n";
            error = true;
            err += L"FAWCheckが選択されましたが、QSVEnc.ini から\n"
                + L"FAW の設定を読み込めませんでした。\n"
                + L"QSVEnc.ini を確認してください。\n";
        } else if (!File::Exists(LocalStg.audEncPath[sys_dat->exstg->s_aud_faw_index])
                   && !check_if_faw2aac_exists()) {
            //fawの実行ファイルが存在しない かつ faw2aacも存在しない
            if (!error) err += L"\n\n";
            error = true;
            err += L"FAWCheckが選択されましたが、FAW(fawcl)へのパスが正しく指定されていません。\n"
                +  L"一度設定画面でFAW(fawcl)へのパスを指定してください。\n";
        }
    }
    if (error) 
        MessageBox::Show(this, err, L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
    return error;
}

System::Boolean frmConfig::CheckVppResolution(CONF_GUIEX *conf) {
    bool error = false;
    if (!conf->qsv.vpp.bUseResize)
        return error;
    int w_mul = 2;
    int h_mul = 2;
    if ((conf->qsv.nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)) != 0 &&
        !conf->qsv.vpp.nDeinterlace)
        h_mul *= 2;
    String^ err = L"";
    if (conf->qsv.nDstWidth % w_mul != 0) {
        err += L"VPP リサイズに指定した横解像度が " + w_mul + L" で割りきれません。\n";
        error = true;
    }
    if (conf->qsv.nDstHeight % h_mul != 0) {
        err += L"VPP リサイズに指定した縦解像度が " + h_mul + L" で割りきれません。\n";
        error = true;
    }
    if (error)
        MessageBox::Show(this, err, L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
    return error;
}

System::Void frmConfig::SaveLocalStg() {
    guiEx_settings *_ex_stg = sys_dat->exstg;
    _ex_stg->load_encode_stg();
    _ex_stg->s_local.large_cmdbox = fcgTXCmd->Multiline;
    GetCHARfromString(_ex_stg->s_local.custom_tmp_dir,        sizeof(_ex_stg->s_local.custom_tmp_dir),        LocalStg.CustomTmpDir);
    GetCHARfromString(_ex_stg->s_local.custom_mp4box_tmp_dir, sizeof(_ex_stg->s_local.custom_mp4box_tmp_dir), LocalStg.CustomMP4TmpDir);
    GetCHARfromString(_ex_stg->s_local.custom_audio_tmp_dir,  sizeof(_ex_stg->s_local.custom_audio_tmp_dir),  LocalStg.CustomAudTmpDir);
    GetCHARfromString(_ex_stg->s_local.app_dir,               sizeof(_ex_stg->s_local.app_dir),               LocalStg.LastAppDir);
    GetCHARfromString(_ex_stg->s_local.bat_dir,               sizeof(_ex_stg->s_local.bat_dir),               LocalStg.LastBatDir);
    GetCHARfromString(_ex_stg->s_mux[MUXER_MP4].fullpath,     sizeof(_ex_stg->s_mux[MUXER_MP4].fullpath),     LocalStg.MP4MuxerPath);
    GetCHARfromString(_ex_stg->s_mux[MUXER_MKV].fullpath,     sizeof(_ex_stg->s_mux[MUXER_MKV].fullpath),     LocalStg.MKVMuxerPath);
    GetCHARfromString(_ex_stg->s_mux[MUXER_TC2MP4].fullpath,  sizeof(_ex_stg->s_mux[MUXER_TC2MP4].fullpath),  LocalStg.TC2MP4Path);
    GetCHARfromString(_ex_stg->s_mux[MUXER_MPG].fullpath,     sizeof(_ex_stg->s_mux[MUXER_MPG].fullpath),     LocalStg.MPGMuxerPath);
    GetCHARfromString(_ex_stg->s_mux[MUXER_MP4_RAW].fullpath, sizeof(_ex_stg->s_mux[MUXER_MP4_RAW].fullpath), LocalStg.MP4RawPath);
    for (int i = 0; i < _ex_stg->s_aud_count; i++)
        GetCHARfromString(_ex_stg->s_aud[i].fullpath,         sizeof(_ex_stg->s_aud[i].fullpath),             LocalStg.audEncPath[i]);
    _ex_stg->save_local();
}

System::Void frmConfig::SetLocalStg() {
    fcgTXMP4MuxerPath->Text       = LocalStg.MP4MuxerPath;
    fcgTXMKVMuxerPath->Text       = LocalStg.MKVMuxerPath;
    fcgTXTC2MP4Path->Text         = LocalStg.TC2MP4Path;
    fcgTXMPGMuxerPath->Text       = LocalStg.MPGMuxerPath;
    fcgTXMP4RawPath->Text         = LocalStg.MP4RawPath;
    fcgTXCustomAudioTempDir->Text = LocalStg.CustomAudTmpDir;
    fcgTXCustomTempDir->Text      = LocalStg.CustomTmpDir;
    fcgTXMP4BoxTempDir->Text      = LocalStg.CustomMP4TmpDir;
    fcgLBMP4MuxerPath->Text       = LocalStg.MP4MuxerExeName + L" の指定";
    fcgLBMKVMuxerPath->Text       = LocalStg.MKVMuxerExeName + L" の指定";
    fcgLBTC2MP4Path->Text         = LocalStg.TC2MP4ExeName   + L" の指定";
    fcgLBMPGMuxerPath->Text       = LocalStg.MPGMuxerExeName + L" の指定";
    fcgLBMP4RawPath->Text         = LocalStg.MP4RawExeName + L" の指定";

    fcgTXMP4MuxerPath->SelectionStart     = fcgTXMP4MuxerPath->Text->Length;
    fcgTXTC2MP4Path->SelectionStart       = fcgTXTC2MP4Path->Text->Length;
    fcgTXMKVMuxerPath->SelectionStart     = fcgTXMKVMuxerPath->Text->Length;
    fcgTXMPGMuxerPath->SelectionStart     = fcgTXMPGMuxerPath->Text->Length;
    fcgTXMP4RawPath->SelectionStart       = fcgTXMP4RawPath->Text->Length;
}

//////////////       その他イベント処理   ////////////////////////
System::Void frmConfig::ActivateToolTip(bool Enable) {
    fcgTTEx->Active = Enable;
}

System::Void frmConfig::fcgTSBOtherSettings_Click(System::Object^  sender, System::EventArgs^  e) {
    frmOtherSettings::Instance::get()->stgDir = String(sys_dat->exstg->s_local.stg_dir).ToString();
    frmOtherSettings::Instance::get()->ShowDialog();
    char buf[MAX_PATH_LEN];
    GetCHARfromString(buf, sizeof(buf), frmOtherSettings::Instance::get()->stgDir);
    if (_stricmp(buf, sys_dat->exstg->s_local.stg_dir)) {
        //変更があったら保存する
        strcpy_s(sys_dat->exstg->s_local.stg_dir, sizeof(sys_dat->exstg->s_local.stg_dir), buf);
        sys_dat->exstg->save_local();
        InitStgFileList();
    }
    //再読み込み
    guiEx_settings stg;
    stg.load_encode_stg();
    log_reload_settings();
    sys_dat->exstg->s_local.default_audio_encoder = stg.s_local.default_audio_encoder;
    sys_dat->exstg->s_local.get_relative_path = stg.s_local.get_relative_path;
    SetStgEscKey(stg.s_local.enable_stg_esc_key != 0);
    ActivateToolTip(stg.s_local.disable_tooltip_help == FALSE);
    if (str_has_char(stg.s_local.conf_font.name))
        SetFontFamilyToForm(this, gcnew FontFamily(String(stg.s_local.conf_font.name).ToString()), this->Font->FontFamily);
}
System::Boolean frmConfig::EnableSettingsNoteChange(bool Enable) {
    if (fcgTSTSettingsNotes->Visible == Enable &&
        fcgTSLSettingsNotes->Visible == !Enable)
        return true;
    if (CountStringBytes(fcgTSTSettingsNotes->Text) > fcgTSTSettingsNotes->MaxLength - 1) {
        MessageBox::Show(this, L"入力された文字数が多すぎます。減らしてください。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
        fcgTSTSettingsNotes->Focus();
        fcgTSTSettingsNotes->SelectionStart = fcgTSTSettingsNotes->Text->Length;
        return false;
    }
    fcgTSTSettingsNotes->Visible = Enable;
    fcgTSLSettingsNotes->Visible = !Enable;
    if (Enable) {
        fcgTSTSettingsNotes->Text = fcgTSLSettingsNotes->Text;
        fcgTSTSettingsNotes->Focus();
        bool isDefaultNote = String::Compare(fcgTSTSettingsNotes->Text, String(DefaultStgNotes).ToString()) == 0;
        fcgTSTSettingsNotes->Select((isDefaultNote) ? 0 : fcgTSTSettingsNotes->Text->Length, fcgTSTSettingsNotes->Text->Length);
    } else {
        SetfcgTSLSettingsNotes(fcgTSTSettingsNotes->Text);
        CheckOtherChanges(nullptr, nullptr);
    }
    return true;
}
System::Void frmConfig::fcgTSLSettingsNotes_DoubleClick(System::Object^  sender, System::EventArgs^  e) {
    EnableSettingsNoteChange(true);
}
System::Void frmConfig::fcgTSTSettingsNotes_Leave(System::Object^  sender, System::EventArgs^  e) {
    EnableSettingsNoteChange(false);
}
System::Void frmConfig::fcgTSTSettingsNotes_KeyDown(System::Object^  sender, System::Windows::Forms::KeyEventArgs^  e) {
    if (e->KeyCode == Keys::Return)
        EnableSettingsNoteChange(false);
}
System::Void frmConfig::fcgTSTSettingsNotes_TextChanged(System::Object^  sender, System::EventArgs^  e) {
    SetfcgTSLSettingsNotes(fcgTSTSettingsNotes->Text);
    CheckOtherChanges(nullptr, nullptr);
}

/////////////    音声設定関連の関数    ///////////////
System::Void frmConfig::fcgCBAudio2pass_CheckedChanged(System::Object^  sender, System::EventArgs^  e) {
    if (fcgCBAudio2pass->Checked) {
        fcgCBAudioUsePipe->Checked = false;
        fcgCBAudioUsePipe->Enabled = false;
    } else if (CurrentPipeEnabled) {
        fcgCBAudioUsePipe->Checked = true;
        fcgCBAudioUsePipe->Enabled = true;
    }
}

System::Void frmConfig::fcgCXAudioEncoder_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
    setAudioDisplay();
}

System::Void frmConfig::fcgCXAudioEncMode_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
    AudioEncodeModeChanged();
}

System::Int32 frmConfig::GetCurrentAudioDefaultBitrate() {
    return sys_dat->exstg->s_aud[fcgCXAudioEncoder->SelectedIndex].mode[fcgCXAudioEncMode->SelectedIndex].bitrate_default;
}

System::Void frmConfig::setAudioDisplay() {
    AUDIO_SETTINGS *astg = &sys_dat->exstg->s_aud[fcgCXAudioEncoder->SelectedIndex];
    //～の指定
    if (str_has_char(astg->filename)) {
        fcgLBAudioEncoderPath->Text = String(astg->filename).ToString() + L" の指定";
        fcgTXAudioEncoderPath->Enabled = true;
        fcgTXAudioEncoderPath->Text = LocalStg.audEncPath[fcgCXAudioEncoder->SelectedIndex];
        fcgBTAudioEncoderPath->Enabled = true;
    } else {
        //filename空文字列(wav出力時)
        fcgLBAudioEncoderPath->Text = L"";
        fcgTXAudioEncoderPath->Enabled = false;
        fcgTXAudioEncoderPath->Text = L"";
        fcgBTAudioEncoderPath->Enabled = false;
    }
    fcgTXAudioEncoderPath->SelectionStart = fcgTXAudioEncoderPath->Text->Length;
    fcgCXAudioEncMode->BeginUpdate();
    fcgCXAudioEncMode->Items->Clear();
    for (int i = 0; i < astg->mode_count; i++)
        fcgCXAudioEncMode->Items->Add(String(astg->mode[i].name).ToString());
    fcgCXAudioEncMode->EndUpdate();
    bool pipe_enabled = (astg->pipe_input && (!(fcgCBAudio2pass->Checked && astg->mode[fcgCXAudioEncMode->SelectedIndex].enc_2pass != 0)));
    CurrentPipeEnabled = pipe_enabled;
    fcgCBAudioUsePipe->Enabled = pipe_enabled;
    fcgCBAudioUsePipe->Checked = pipe_enabled;
    if (fcgCXAudioEncMode->Items->Count > 0)
        fcgCXAudioEncMode->SelectedIndex = 0;
}

System::Void frmConfig::AudioEncodeModeChanged() {
    int index = fcgCXAudioEncMode->SelectedIndex;
    AUDIO_SETTINGS *astg = &sys_dat->exstg->s_aud[fcgCXAudioEncoder->SelectedIndex];
    if (astg->mode[index].bitrate) {
        fcgCXAudioEncMode->Width = fcgCXAudioEncModeSmallWidth;
        fcgLBAudioBitrate->Visible = true;
        fcgNUAudioBitrate->Visible = true;
        fcgNUAudioBitrate->Minimum = astg->mode[index].bitrate_min;
        fcgNUAudioBitrate->Maximum = astg->mode[index].bitrate_max;
        fcgNUAudioBitrate->Increment = astg->mode[index].bitrate_step;
        SetNUValue(fcgNUAudioBitrate, (conf->aud.bitrate != 0) ? conf->aud.bitrate : astg->mode[index].bitrate_default);
    } else {
        fcgCXAudioEncMode->Width = fcgCXAudioEncModeLargeWidth;
        fcgLBAudioBitrate->Visible = false;
        fcgNUAudioBitrate->Visible = false;
        fcgNUAudioBitrate->Minimum = 0;
        fcgNUAudioBitrate->Maximum = 1536; //音声の最大レートは1536kbps
    }
    fcgCBAudio2pass->Enabled = astg->mode[index].enc_2pass != 0;
    if (!fcgCBAudio2pass->Enabled) fcgCBAudio2pass->Checked = false;
    SetfbcBTABEnable(fcgNUAudioBitrate->Visible, (int)fcgNUAudioBitrate->Maximum);

    bool delay_cut_available = astg->mode[index].delay > 0;
    fcgLBAudioDelayCut->Visible = delay_cut_available;
    fcgCXAudioDelayCut->Visible = delay_cut_available;
    if (delay_cut_available) {
        const bool delay_cut_edts_available = str_has_char(astg->cmd_raw) && str_has_char(sys_dat->exstg->s_mux[MUXER_MP4_RAW].delay_cmd);
        const int current_idx = fcgCXAudioDelayCut->SelectedIndex;
        const int items_to_set = _countof(AUDIO_DELAY_CUT_MODE) - 1 - ((delay_cut_edts_available) ? 0 : 1);
        fcgCXAudioDelayCut->BeginUpdate();
        fcgCXAudioDelayCut->Items->Clear();
        for (int i = 0; i < items_to_set; i++)
            fcgCXAudioDelayCut->Items->Add(String(AUDIO_DELAY_CUT_MODE[i]).ToString());
        fcgCXAudioDelayCut->EndUpdate();
        fcgCXAudioDelayCut->SelectedIndex = (current_idx >= items_to_set) ? 0 : current_idx;
    } else {
        fcgCXAudioDelayCut->SelectedIndex = 0;
    }
}

///////////////   設定ファイル関連   //////////////////////
System::Void frmConfig::CheckTSItemsEnabled(CONF_GUIEX *current_conf) {
    bool selected = (CheckedStgMenuItem != nullptr);
    fcgTSBSave->Enabled = (selected && memcmp(cnf_stgSelected, current_conf, sizeof(CONF_GUIEX)));
    fcgTSBDelete->Enabled = selected;
}

System::Void frmConfig::UncheckAllDropDownItem(ToolStripItem^ mItem) {
    ToolStripDropDownItem^ DropDownItem = dynamic_cast<ToolStripDropDownItem^>(mItem);
    if (DropDownItem == nullptr)
        return;
    for (int i = 0; i < DropDownItem->DropDownItems->Count; i++) {
        UncheckAllDropDownItem(DropDownItem->DropDownItems[i]);
        ToolStripMenuItem^ item = dynamic_cast<ToolStripMenuItem^>(DropDownItem->DropDownItems[i]);
        if (item != nullptr)
            item->Checked = false;
    }
}

System::Void frmConfig::CheckTSSettingsDropDownItem(ToolStripMenuItem^ mItem) {
    UncheckAllDropDownItem(fcgTSSettings);
    CheckedStgMenuItem = mItem;
    fcgTSSettings->Text = (mItem == nullptr) ? L"プロファイル" : mItem->Text;
    if (mItem != nullptr)
        mItem->Checked = true;
    fcgTSBSave->Enabled = false;
    fcgTSBDelete->Enabled = (mItem != nullptr);
}

ToolStripMenuItem^ frmConfig::fcgTSSettingsSearchItem(String^ stgPath, ToolStripItem^ mItem) {
    if (stgPath == nullptr)
        return nullptr;
    ToolStripDropDownItem^ DropDownItem = dynamic_cast<ToolStripDropDownItem^>(mItem);
    if (DropDownItem == nullptr)
        return nullptr;
    for (int i = 0; i < DropDownItem->DropDownItems->Count; i++) {
        ToolStripMenuItem^ item = fcgTSSettingsSearchItem(stgPath, DropDownItem->DropDownItems[i]);
        if (item != nullptr)
            return item;
        item = dynamic_cast<ToolStripMenuItem^>(DropDownItem->DropDownItems[i]);
        if (item      != nullptr && 
            item->Tag != nullptr && 
            0 == String::Compare(item->Tag->ToString(), stgPath, true))
            return item;
    }
    return nullptr;
}

ToolStripMenuItem^ frmConfig::fcgTSSettingsSearchItem(String^ stgPath) {
    return fcgTSSettingsSearchItem((stgPath != nullptr && stgPath->Length > 0) ? Path::GetFullPath(stgPath) : nullptr, fcgTSSettings);
}

System::Void frmConfig::SaveToStgFile(String^ stgName) {
    size_t nameLen = CountStringBytes(stgName) + 1; 
    char *stg_name = (char *)malloc(nameLen);
    GetCHARfromString(stg_name, nameLen, stgName);
    init_CONF_GUIEX(cnf_stgSelected, FALSE);
    FrmToConf(cnf_stgSelected);
    String^ stgDir = Path::GetDirectoryName(stgName);
    if (!Directory::Exists(stgDir))
        Directory::CreateDirectory(stgDir);
    guiEx_config exCnf;
    int result = exCnf.save_qsvp_conf(cnf_stgSelected, stg_name);
    free(stg_name);
    switch (result) {
        case CONF_ERROR_FILE_OPEN:
            MessageBox::Show(L"設定ファイルオープンに失敗しました。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
            return;
        case CONF_ERROR_INVALID_FILENAME:
            MessageBox::Show(L"ファイル名に使用できない文字が含まれています。\n保存できません。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
            return;
        default:
            break;
    }
    init_CONF_GUIEX(cnf_stgSelected, FALSE);
    FrmToConf(cnf_stgSelected);
}

System::Void frmConfig::fcgTSBSave_Click(System::Object^  sender, System::EventArgs^  e) {
    if (CheckedStgMenuItem != nullptr)
        SaveToStgFile(CheckedStgMenuItem->Tag->ToString());
    CheckTSSettingsDropDownItem(CheckedStgMenuItem);
}

System::Void frmConfig::fcgTSBSaveNew_Click(System::Object^  sender, System::EventArgs^  e) {
    frmSaveNewStg::Instance::get()->setStgDir(String(sys_dat->exstg->s_local.stg_dir).ToString());
    if (CheckedStgMenuItem != nullptr)
        frmSaveNewStg::Instance::get()->setFilename(CheckedStgMenuItem->Text);
    frmSaveNewStg::Instance::get()->ShowDialog();
    String^ stgName = frmSaveNewStg::Instance::get()->StgFileName;
    if (stgName != nullptr && stgName->Length)
        SaveToStgFile(stgName);
    RebuildStgFileDropDown(nullptr);
    CheckTSSettingsDropDownItem(fcgTSSettingsSearchItem(stgName));
}

System::Void frmConfig::DeleteStgFile(ToolStripMenuItem^ mItem) {
    if (System::Windows::Forms::DialogResult::OK ==
        MessageBox::Show(L"設定ファイル " + mItem->Text + L" を削除してよろしいですか?",
        L"エラー", MessageBoxButtons::OKCancel, MessageBoxIcon::Exclamation)) 
    {
        File::Delete(mItem->Tag->ToString());
        RebuildStgFileDropDown(nullptr);
        CheckTSSettingsDropDownItem(nullptr);
        SetfcgTSLSettingsNotes(L"");
    }
}

System::Void frmConfig::fcgTSBDelete_Click(System::Object^  sender, System::EventArgs^  e) {
    DeleteStgFile(CheckedStgMenuItem);
}

System::Void frmConfig::fcgTSSettings_DropDownItemClicked(System::Object^  sender, System::Windows::Forms::ToolStripItemClickedEventArgs^  e) {
    ToolStripMenuItem^ ClickedMenuItem = dynamic_cast<ToolStripMenuItem^>(e->ClickedItem);
    if (ClickedMenuItem == nullptr)
        return;
    if (ClickedMenuItem->Tag == nullptr || ClickedMenuItem->Tag->ToString()->Length == 0)
        return;
    CONF_GUIEX load_stg;
    guiEx_config exCnf;
    char stg_path[MAX_PATH_LEN];
    GetCHARfromString(stg_path, sizeof(stg_path), ClickedMenuItem->Tag->ToString());
    if (exCnf.load_qsvp_conf(&load_stg, stg_path) == CONF_ERROR_FILE_OPEN) {
        if (MessageBox::Show(L"設定ファイルオープンに失敗しました。\n"
                           + L"このファイルを削除しますか?",
                           L"エラー", MessageBoxButtons::YesNo, MessageBoxIcon::Error)
                           == System::Windows::Forms::DialogResult::Yes)
            DeleteStgFile(ClickedMenuItem);
        return;
    }
    ConfToFrm(&load_stg);
    CheckTSSettingsDropDownItem(ClickedMenuItem);
    memcpy(cnf_stgSelected, &load_stg, sizeof(CONF_GUIEX));
}

System::Void frmConfig::RebuildStgFileDropDown(ToolStripDropDownItem^ TS, String^ dir) {
    array<String^>^ subDirs = Directory::GetDirectories(dir);
    for (int i = 0; i < subDirs->Length; i++) {
        ToolStripMenuItem^ DDItem = gcnew ToolStripMenuItem(L"[ " + subDirs[i]->Substring(dir->Length+1) + L" ]");
        DDItem->DropDownItemClicked += gcnew System::Windows::Forms::ToolStripItemClickedEventHandler(this, &frmConfig::fcgTSSettings_DropDownItemClicked);
        DDItem->ForeColor = Color::Blue;
        DDItem->Tag = nullptr;
        RebuildStgFileDropDown(DDItem, subDirs[i]);
        TS->DropDownItems->Add(DDItem);
    }
    array<String^>^ stgList = Directory::GetFiles(dir, L"*.stg");
    for (int i = 0; i < stgList->Length; i++) {
        ToolStripMenuItem^ mItem = gcnew ToolStripMenuItem(Path::GetFileNameWithoutExtension(stgList[i]));
        mItem->Tag = stgList[i];
        TS->DropDownItems->Add(mItem);
    }
}

System::Void frmConfig::RebuildStgFileDropDown(String^ stgDir) {
    fcgTSSettings->DropDownItems->Clear();
    if (stgDir != nullptr)
        CurrentStgDir = stgDir;
    if (!Directory::Exists(CurrentStgDir))
        Directory::CreateDirectory(CurrentStgDir);
    RebuildStgFileDropDown(fcgTSSettings, Path::GetFullPath(CurrentStgDir));
}

//////////////   初期化関連     ////////////////
System::Void frmConfig::InitData(CONF_GUIEX *set_config, const SYSTEM_DATA *system_data) {
    if (set_config->size_all != CONF_INITIALIZED) {
        //初期化されていなければ初期化する
        init_CONF_GUIEX(set_config, FALSE);
    }
    conf = set_config;
    sys_dat = system_data;
}

System::Void frmConfig::InitComboBox() {
    //コンボボックスに値を設定する
    setComboBox(fcgCXEncMode,         list_encmode);
    setComboBox(fcgCXOutputType,      list_outtype);
    setComboBox(fcgCXCodecLevel,      list_avc_level);
    setComboBox(fcgCXCodecProfile,    list_avc_profile);
    setComboBox(fcgCXQuality,         list_quality);
    setComboBox(fcgCXInterlaced,      list_interlaced);
    setComboBox(fcgCXAspectRatio,     list_aspect_ratio);
    setComboBox(fcgCXTrellis,         list_avc_trellis);
    setComboBox(fcgCXLookaheadDS,     list_lookahead_ds);
    
    setComboBox(fcgCXMVPred,          list_mv_presicion);
    setComboBox(fcgCXInterPred,       list_pred_block_size);
    setComboBox(fcgCXIntraPred,       list_pred_block_size);
    
    setComboBox(fcgCXMVCostScaling,   list_mv_cost_scaling);

    setComboBox(fcgCXAudioTempDir,    list_audtempdir);
    setComboBox(fcgCXMP4BoxTempDir,   list_mp4boxtempdir);
    setComboBox(fcgCXTempDir,         list_tempdir);

    setComboBox(fcgCXColorPrim,       list_colorprim);
    setComboBox(fcgCXColorMatrix,     list_colormatrix);
    setComboBox(fcgCXTransfer,        list_transfer);
    setComboBox(fcgCXVideoFormat,     list_videoformat);

    setComboBox(fcgCXDeinterlace,     list_deinterlace_ja);
    setComboBox(fcgCXTelecinePatterns,list_telecine_patterns);
    setComboBox(fcgCXFPSConversion,   list_vpp_fps_conversion);
    setComboBox(fcgCXImageStabilizer, list_vpp_image_stabilizer);

    setComboBox(fcgCXAudioEncTiming, audio_enc_timing_desc);
    setComboBox(fcgCXAudioDelayCut,  AUDIO_DELAY_CUT_MODE);

    setMuxerCmdExNames(fcgCXMP4CmdEx, MUXER_MP4);
    setMuxerCmdExNames(fcgCXMKVCmdEx, MUXER_MKV);
#ifdef HIDE_MPEG2
    fcgCXMPGCmdEx->Items->Clear();
    fcgCXMPGCmdEx->Items->Add("");
#else
    setMuxerCmdExNames(fcgCXMPGCmdEx, MUXER_MPG);
#endif

    setAudioEncoderNames();

    setPriorityList(fcgCXMuxPriority);
    setPriorityList(fcgCXAudioPriority);
}

System::Void frmConfig::SetTXMaxLen(TextBox^ TX, int max_len) {
    TX->MaxLength = max_len;
    TX->Validating += gcnew System::ComponentModel::CancelEventHandler(this, &frmConfig::TX_LimitbyBytes);
}

System::Void frmConfig::SetTXMaxLenAll() {
    //MaxLengthに最大文字数をセットし、それをもとにバイト数計算を行うイベントをセットする。
    SetTXMaxLen(fcgTXAudioEncoderPath,   sizeof(sys_dat->exstg->s_aud[0].fullpath) - 1);
    SetTXMaxLen(fcgTXMP4MuxerPath,       sizeof(sys_dat->exstg->s_mux[MUXER_MP4].fullpath) - 1);
    SetTXMaxLen(fcgTXMKVMuxerPath,       sizeof(sys_dat->exstg->s_mux[MUXER_MKV].fullpath) - 1);
    SetTXMaxLen(fcgTXTC2MP4Path,         sizeof(sys_dat->exstg->s_mux[MUXER_TC2MP4].fullpath) - 1);
    SetTXMaxLen(fcgTXMPGMuxerPath,       sizeof(sys_dat->exstg->s_mux[MUXER_MPG].fullpath) - 1);
    SetTXMaxLen(fcgTXMP4RawPath,         sizeof(sys_dat->exstg->s_mux[MUXER_MP4_RAW].fullpath) - 1);
    SetTXMaxLen(fcgTXCustomTempDir,      sizeof(sys_dat->exstg->s_local.custom_tmp_dir) - 1);
    SetTXMaxLen(fcgTXCustomAudioTempDir, sizeof(sys_dat->exstg->s_local.custom_audio_tmp_dir) - 1);
    SetTXMaxLen(fcgTXMP4BoxTempDir,      sizeof(sys_dat->exstg->s_local.custom_mp4box_tmp_dir) - 1);
    SetTXMaxLen(fcgTXBatBeforeAudioPath, sizeof(conf->oth.batfile.before_audio) - 1);
    SetTXMaxLen(fcgTXBatAfterAudioPath,  sizeof(conf->oth.batfile.after_audio) - 1);
    SetTXMaxLen(fcgTXBatBeforePath,      sizeof(conf->oth.batfile.before_process) - 1);
    SetTXMaxLen(fcgTXBatAfterPath,       sizeof(conf->oth.batfile.after_process) - 1);
    fcgTSTSettingsNotes->MaxLength     = sizeof(conf->oth.notes) - 1;
}

System::Void frmConfig::InitStgFileList() {
    RebuildStgFileDropDown(String(sys_dat->exstg->s_local.stg_dir).ToString());
    stgChanged = false;
    CheckTSSettingsDropDownItem(nullptr);
}

System::Boolean frmConfig::fcgCheckRCModeLibVersion(int rc_mode_target, int rc_mode_replace, bool mode_supported) {
    System::Boolean selected_idx_changed = false;
    int encmode_idx = get_cx_index(list_encmode, rc_mode_target);
    if (encmode_idx < 0)
        return false;
    if (mode_supported) {
        fcgCXEncMode->Items[encmode_idx] = String(list_encmode[encmode_idx].desc).ToString();
    } else {
        fcgCXEncMode->Items[encmode_idx] = L"-----------------";
        if (fcgCXEncMode->SelectedIndex == encmode_idx) {
            fcgCXEncMode->SelectedIndex = get_cx_index(list_encmode, rc_mode_replace);
            selected_idx_changed = true;
        }
    }
    return selected_idx_changed;
}

System::Boolean frmConfig::fcgCheckLibRateControl(mfxU32 mfxlib_current, mfxU64 available_features) {
    System::Boolean result = false;
    fcgCXEncMode->SelectedIndexChanged -= gcnew System::EventHandler(this, &frmConfig::CheckOtherChanges);
    fcgCXEncMode->SelectedIndexChanged -= gcnew System::EventHandler(this, &frmConfig::fcgChangeEnabled);
    if (   fcgCheckRCModeLibVersion(MFX_RATECONTROL_AVBR,   MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_AVBR))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_QVBR,   MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_QVBR))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA,     MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_LA))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA_EXT, MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_LA_EXT))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA_HRD, MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_LA_HRD))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_ICQ,    MFX_RATECONTROL_CQP, 0 != (available_features & ENC_FEATURE_ICQ))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA_ICQ, MFX_RATECONTROL_CQP, (ENC_FEATURE_LA | ENC_FEATURE_ICQ) == (available_features & (ENC_FEATURE_LA | ENC_FEATURE_ICQ)))
        || fcgCheckRCModeLibVersion(MFX_RATECONTROL_VCM,    MFX_RATECONTROL_VQP, 0 != (available_features & ENC_FEATURE_VCM)))
        result = true;
    fcgCXEncMode->SelectedIndexChanged += gcnew System::EventHandler(this, &frmConfig::fcgChangeEnabled);
    fcgCXEncMode->SelectedIndexChanged += gcnew System::EventHandler(this, &frmConfig::CheckOtherChanges);
    return result;
}

System::Void frmConfig::fcgCheckLibVersion(mfxU32 mfxlib_current, mfxU64 available_features) {
    if (0 == mfxlib_current)
        return;

    fcgCXEncMode->SelectedIndexChanged -= gcnew System::EventHandler(this, &frmConfig::CheckOtherChanges);
    fcgCXEncMode->SelectedIndexChanged -= gcnew System::EventHandler(this, &frmConfig::fcgChangeEnabled);
    
    fcgPNExtSettings->Visible = !fcgCBHWEncode->Checked;

    //Scene change detection
    fcgCBSceneChange->Enabled = 0 != (available_features & ENC_FEATURE_SCENECHANGE);
    if (!fcgCBSceneChange->Enabled) fcgCBSceneChange->Checked = false;
    
    //API v1.3 features
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_AVBR, MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_AVBR));
    fcgLBVideoFormat->Enabled = 0 != (available_features & ENC_FEATURE_VUI_INFO);
    fcgCXVideoFormat->Enabled = 0 != (available_features & ENC_FEATURE_VUI_INFO);
    fcgLBFullrange->Enabled   = 0 != (available_features & ENC_FEATURE_VUI_INFO);
    fcgCBFullrange->Enabled   = 0 != (available_features & ENC_FEATURE_VUI_INFO);
    fcggroupBoxColor->Enabled = 0 != (available_features & ENC_FEATURE_VUI_INFO);
    if (!fcgCXVideoFormat->Enabled) fcgCXVideoFormat->SelectedIndex = 0;
    if (!fcgCBFullrange->Enabled)   fcgCBFullrange->Checked = false;
    if (!fcggroupBoxColor->Enabled) {
        fcgCXColorMatrix->SelectedIndex = 0;
        fcgCXColorPrim->SelectedIndex = 0;
        fcgCXTransfer->SelectedIndex = 0;
    }

    //API v1.6 features
    fcgCBExtBRC->Enabled = 0 != (available_features & ENC_FEATURE_EXT_BRC);
    fcgCBMBBRC->Enabled  = 0 != (available_features & ENC_FEATURE_MBBRC);
    if (!fcgCBExtBRC->Enabled) fcgCBExtBRC->Checked = false;
    if (!fcgCBMBBRC->Enabled)  fcgCBMBBRC->Checked = false;
    
    //API v1.7 features
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA, MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_LA));
    fcgLBTrellis->Enabled = 0 != (available_features & ENC_FEATURE_TRELLIS);
    fcgCXTrellis->Enabled = 0 != (available_features & ENC_FEATURE_TRELLIS);
    if (!fcgCXTrellis->Enabled) fcgCXTrellis->SelectedIndex = 0;
    
    //API v1.8 features
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_ICQ,    MFX_RATECONTROL_CQP, 0 != (available_features & ENC_FEATURE_ICQ));
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA_ICQ, MFX_RATECONTROL_CQP, (ENC_FEATURE_LA | ENC_FEATURE_ICQ) == (available_features & (ENC_FEATURE_LA | ENC_FEATURE_ICQ)));
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_VCM,    MFX_RATECONTROL_VQP, 0 != (available_features & ENC_FEATURE_VCM));
    fcgCBAdaptiveB->Enabled   = 0 != (available_features & ENC_FEATURE_ADAPTIVE_B);
    fcgCBAdaptiveI->Enabled   = 0 != (available_features & ENC_FEATURE_ADAPTIVE_I);
    fcgCBBPyramid->Enabled    = 0 != (available_features & ENC_FEATURE_B_PYRAMID);
    fcgLBLookaheadDS->Enabled = 0 != (available_features & ENC_FEATURE_LA_DS);
    fcgCXLookaheadDS->Enabled = 0 != (available_features & ENC_FEATURE_LA_DS);
    if (!fcgCBAdaptiveB->Enabled)   fcgCBAdaptiveB->Checked = false;
    if (!fcgCBAdaptiveI->Enabled)   fcgCBAdaptiveI->Checked = false;
    if (!fcgCBBPyramid->Enabled)    fcgCBBPyramid->Checked  = false;
    if (!fcgCXLookaheadDS->Enabled) fcgCXLookaheadDS->SelectedIndex = 0;
    
    //API v1.9 features
    fcgNUQPMin->Enabled        = 0 != (available_features & ENC_FEATURE_QP_MINMAX);
    fcgNUQPMax->Enabled        = 0 != (available_features & ENC_FEATURE_QP_MINMAX);
    fcgCBIntraRefresh->Enabled = 0 != (available_features & ENC_FEATURE_INTRA_REFRESH);
    fcgCBDeblock->Enabled      = 0 != (available_features & ENC_FEATURE_NO_DEBLOCK);
    if (!fcgNUQPMin->Enabled)        fcgNUQPMin->Value = 0;
    if (!fcgNUQPMax->Enabled)        fcgNUQPMax->Value = 0;
    if (!fcgCBIntraRefresh->Enabled) fcgCBIntraRefresh->Checked = false;
    if (!fcgCBDeblock->Enabled)      fcgCBDeblock->Checked = true;

    //API v1.11 features
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA_HRD, MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_LA_HRD));
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_LA_EXT, MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_LA_EXT));
    fcgCheckRCModeLibVersion(MFX_RATECONTROL_QVBR,   MFX_RATECONTROL_VBR, 0 != (available_features & ENC_FEATURE_QVBR));
    fcgLBWinBRCSize->Enabled     = 0 != (available_features & ENC_FEATURE_WINBRC);
    fcgLBWinBRCSizeAuto->Enabled = 0 != (available_features & ENC_FEATURE_WINBRC);
    fcgNUWinBRCSize->Enabled     = 0 != (available_features & ENC_FEATURE_WINBRC);
    if (!fcgNUWinBRCSize->Enabled) fcgNUWinBRCSize->Value = 0;

    //API v1.13 features
    fcgLBMVCostScaling->Enabled    = 0 != (available_features & ENC_FEATURE_GLOBAL_MOTION_ADJUST);
    fcgCXMVCostScaling->Enabled    = 0 != (available_features & ENC_FEATURE_GLOBAL_MOTION_ADJUST);
    fcgCBDirectBiasAdjust->Enabled = 0 != (available_features & ENC_FEATURE_DIRECT_BIAS_ADJUST);
    if (!fcgCXMVCostScaling->Enabled)    fcgCXMVCostScaling->SelectedIndex = 0;
    if (!fcgCBDirectBiasAdjust->Enabled) fcgCBDirectBiasAdjust->Checked = false;

    //API v1.15 features
    fcgCBFixedFunc->Enabled        = 0 != (available_features & ENC_FEATURE_FIXED_FUNC);
    if (!fcgCBFixedFunc->Enabled) fcgCBFixedFunc->Checked = false;

    fcgCXEncMode->SelectedIndexChanged += gcnew System::EventHandler(this, &frmConfig::fcgChangeEnabled);
    fcgCXEncMode->SelectedIndexChanged += gcnew System::EventHandler(this, &frmConfig::CheckOtherChanges);
}

System::Void frmConfig::fcgChangeEnabled(System::Object^  sender, System::EventArgs^  e) {
    //両方ともnullptrの組み合わせ、つまりInitFormで呼ばれた場合以外では、
    //もしfeatureListが作成できていなければ、チェックを行わない
    if (sender != nullptr && e != nullptr) {
        bool featureListAvialable = (fcgCBHWEncode->Checked) ? featuresHW->checkIfGetFeaturesFinished() : featuresSW->checkIfGetFeaturesFinished();
        if (!featureListAvialable)
            return;
    }

    this->SuspendLayout();

    const mfxU32 codecId = list_outtype[fcgCXOutputType->SelectedIndex].value;

    mfxVersion mfxlib_target;
    mfxlib_target.Version = (fcgCBHWEncode->Checked) ? featuresHW->GetmfxLibVer() : featuresSW->GetmfxLibVer();
    
    mfxU64 available_features = (fcgCBHWEncode->Checked) ? featuresHW->getFeatureOfRC(fcgCXEncMode->SelectedIndex, codecId) : featuresSW->getFeatureOfRC(fcgCXEncMode->SelectedIndex, codecId);
    //まず、レート制御モードのみのチェックを行う
    //もし、レート制御モードの更新が必要ならavailable_featuresの更新も行う
    if (fcgCheckLibRateControl(mfxlib_target.Version, available_features))
        available_features = (fcgCBHWEncode->Checked) ? featuresHW->getFeatureOfRC(fcgCXEncMode->SelectedIndex, codecId) : featuresSW->getFeatureOfRC(fcgCXEncMode->SelectedIndex, codecId);

    //つぎに全体のチェックを行う
    fcgCheckLibVersion(mfxlib_target.Version, available_features);
    int enc_mode = list_encmode[fcgCXEncMode->SelectedIndex].value;
    bool cqp_mode =     (enc_mode == MFX_RATECONTROL_CQP    || enc_mode == MFX_RATECONTROL_VQP);
    bool avbr_mode =    (enc_mode == MFX_RATECONTROL_AVBR);
    bool cbr_vbr_mode = (enc_mode == MFX_RATECONTROL_VBR    || enc_mode == MFX_RATECONTROL_CBR);
    bool la_mode =      (rc_is_type_lookahead(enc_mode));
    bool icq_mode =     (enc_mode == MFX_RATECONTROL_LA_ICQ || enc_mode == MFX_RATECONTROL_ICQ);
    bool qvbr_mode =    (enc_mode == MFX_RATECONTROL_QVBR);
    //bool vcm_mode =     (enc_mode == MFX_RATECONTROL_VCM);

    fcgPNQP->Visible = cqp_mode;
    fcgNUQPI->Enabled = cqp_mode;
    fcgNUQPP->Enabled = cqp_mode;
    fcgNUQPB->Enabled = cqp_mode;
    fcgLBQPI->Enabled = cqp_mode;
    fcgLBQPP->Enabled = cqp_mode;
    fcgLBQPB->Enabled = cqp_mode;

    fcgPNBitrate->Visible = !cqp_mode;
    fcgNUBitrate->Enabled = !cqp_mode;
    fcgLBBitrate->Enabled = !cqp_mode;
    bool enableMaxKbps = cbr_vbr_mode || qvbr_mode || (available_features & ENC_FEATURE_WINBRC);
    fcgNUMaxkbps->Enabled = enableMaxKbps;
    fcgLBMaxkbps->Enabled = enableMaxKbps;
    fcgLBMaxBitrate2->Enabled = enableMaxKbps;

    fcgPNAVBR->Visible = avbr_mode;
    fcgLBAVBRAccuarcy->Enabled = avbr_mode;
    fcgLBAVBRAccuarcy2->Enabled = avbr_mode;
    fcgNUAVBRAccuarcy->Enabled = avbr_mode;
    fcgLBAVBRConvergence->Enabled = avbr_mode;
    fcgLBAVBRConvergence2->Enabled = avbr_mode;
    fcgNUAVBRConvergence->Enabled = avbr_mode;

    fcgPNLookahead->Visible = la_mode;
    fcgLBLookaheadDepth->Enabled = la_mode;
    fcgNULookaheadDepth->Enabled = la_mode;
    fcgLBLookaheadDS->Visible = la_mode;
    fcgCXLookaheadDS->Visible = la_mode;
    fcgLBWinBRCSize->Visible = la_mode;
    fcgLBWinBRCSizeAuto->Visible = la_mode;
    fcgNUWinBRCSize->Visible = la_mode;

    fcgNURef->Visible = !fcgCBHWEncode->Checked;
    fcgLBRef->Visible = !fcgCBHWEncode->Checked;
    fcgLBRefAuto->Visible = !fcgCBHWEncode->Checked;
    fcgCBD3DMemAlloc->Enabled = fcgCBHWEncode->Checked;
    fcgCBD3DMemAlloc->Checked = fcgCBD3DMemAlloc->Enabled;

    fcggroupBoxVpp->Enabled = fcgCBUseVpp->Checked;

    fcgPNICQ->Visible = icq_mode;
    fcgPNQVBR->Visible = qvbr_mode;

    fcggroupBoxVppResize->Enabled = fcgCBVppResize->Checked;
    fcggroupBoxVppDenoise->Enabled = fcgCBVppDenoise->Checked;
    fcggroupBoxVppDetail->Enabled = fcgCBVppDetail->Checked;
    fcgCXTelecinePatterns->Visible = fcgCXDeinterlace->SelectedIndex == get_cx_index(list_deinterlace_ja, MFX_DEINTERLACE_IT_MANUAL);

    this->ResumeLayout();
    this->PerformLayout();
}

System::Void frmConfig::fcgCheckVppFeatures() {
    UInt64 available_features = (fcgCBHWEncode->Checked) ? featuresHW->getVppFeatures() : featuresSW->getVppFeatures();
    fcgCBVppResize->Enabled = 0 != (available_features & VPP_FEATURE_RESIZE);
    if (!fcgCBVppResize->Enabled) fcgCBVppResize->Checked;
    
    fcgCBVppDenoise->Enabled = 0 != (available_features & VPP_FEATURE_DENOISE);
    if (!fcgCBVppDenoise->Enabled) fcgCBVppDenoise->Checked;

    fcgCBVppDetail->Enabled = 0 != (available_features & VPP_FEATURE_DETAIL_ENHANCEMENT);
    if (!fcgCBVppDetail->Enabled) fcgCBVppDetail->Checked;

#if ENABLE_FPS_CONVERSION
    fcgCXFPSConversion->Enabled = 0 != (available_features & VPP_FEATURE_FPS_CONVERSION_ADV);
    fcgLBFPSConversion->Enabled = fcgCXFPSConversion->Enabled;
    if (!fcgCXFPSConversion->Enabled) fcgCXFPSConversion->SelectedIndex = 0;
#else
    //うまくうごいてなさそうなので無効化
    fcgCXFPSConversion->Visible = false;
    fcgLBFPSConversion->Visible = false;
    fcgCXFPSConversion->SelectedIndex = 0;
#endif

    fcgCXImageStabilizer->Enabled = 0 != (available_features & VPP_FEATURE_IMAGE_STABILIZATION);
    fcgLBImageStabilizer->Enabled = fcgCXImageStabilizer->Enabled;
    if (!fcgCXImageStabilizer->Enabled) fcgCXImageStabilizer->SelectedIndex = 0;
}

System::Void frmConfig::fcgCBHWLibChanged(System::Object^  sender, System::EventArgs^  e) {
    UpdateFeatures();
}

System::Void frmConfig::fcgCXOutputType_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
    this->SuspendLayout();

    setComboBox(fcgCXCodecLevel,   get_level_list(list_outtype[fcgCXOutputType->SelectedIndex].value));
    setComboBox(fcgCXCodecProfile, get_profile_list(list_outtype[fcgCXOutputType->SelectedIndex].value));
    fcgCXCodecLevel->SelectedIndex = 0;
    fcgCXCodecProfile->SelectedIndex = 0;

    UpdateFeatures();
    fcgChangeEnabled(sender, e);

    this->ResumeLayout();
    this->PerformLayout();
}

System::Void frmConfig::fcgChangeMuxerVisible(System::Object^  sender, System::EventArgs^  e) {
    //tc2mp4のチェック
    const bool enable_tc2mp4_muxer = (0 != str_has_char(sys_dat->exstg->s_mux[MUXER_TC2MP4].base_cmd));
    fcgTXTC2MP4Path->Visible = enable_tc2mp4_muxer;
    fcgLBTC2MP4Path->Visible = enable_tc2mp4_muxer;
    fcgBTTC2MP4Path->Visible = enable_tc2mp4_muxer;
    fcgCBAFS->Enabled        = enable_tc2mp4_muxer;
    fcgCBAuoTcfileout->Enabled = enable_tc2mp4_muxer;
    //mp4 rawのチェック
    const bool enable_mp4raw_muxer = (0 != str_has_char(sys_dat->exstg->s_mux[MUXER_MP4_RAW].base_cmd));
    fcgTXMP4RawPath->Visible = enable_mp4raw_muxer;
    fcgLBMP4RawPath->Visible = enable_mp4raw_muxer;
    fcgBTMP4RawPath->Visible = enable_mp4raw_muxer;
    //一時フォルダのチェック
    const bool enable_mp4_tmp = (0 != str_has_char(sys_dat->exstg->s_mux[MUXER_MP4].tmp_cmd));
    fcgCXMP4BoxTempDir->Visible = enable_mp4_tmp;
    fcgLBMP4BoxTempDir->Visible = enable_mp4_tmp;
    fcgTXMP4BoxTempDir->Visible = enable_mp4_tmp;
    fcgBTMP4BoxTempDir->Visible = enable_mp4_tmp;
    //Apple Chapterのチェック
    bool enable_mp4_apple_cmdex = false;
    for (int i = 0; i < sys_dat->exstg->s_mux[MUXER_MP4].ex_count; i++)
        enable_mp4_apple_cmdex |= (0 != str_has_char(sys_dat->exstg->s_mux[MUXER_MP4].ex_cmd[i].cmd_apple));
    fcgCBMP4MuxApple->Visible = enable_mp4_apple_cmdex;

    //位置の調整
    static const int HEIGHT = 31;
    fcgLBTC2MP4Path->Location = Point(fcgLBTC2MP4Path->Location.X, fcgLBMP4MuxerPath->Location.Y + HEIGHT * enable_tc2mp4_muxer);
    fcgTXTC2MP4Path->Location = Point(fcgTXTC2MP4Path->Location.X, fcgTXMP4MuxerPath->Location.Y + HEIGHT * enable_tc2mp4_muxer);
    fcgBTTC2MP4Path->Location = Point(fcgBTTC2MP4Path->Location.X, fcgBTMP4MuxerPath->Location.Y + HEIGHT * enable_tc2mp4_muxer);
    fcgLBMP4RawPath->Location = Point(fcgLBMP4RawPath->Location.X, fcgLBTC2MP4Path->Location.Y   + HEIGHT * enable_mp4raw_muxer);
    fcgTXMP4RawPath->Location = Point(fcgTXMP4RawPath->Location.X, fcgTXTC2MP4Path->Location.Y   + HEIGHT * enable_mp4raw_muxer);
    fcgBTMP4RawPath->Location = Point(fcgBTMP4RawPath->Location.X, fcgBTTC2MP4Path->Location.Y   + HEIGHT * enable_mp4raw_muxer);
}

System::Void frmConfig::SetStgEscKey(bool Enable) {
    if (this->KeyPreview == Enable)
        return;
    this->KeyPreview = Enable;
    if (Enable)
        this->KeyDown += gcnew System::Windows::Forms::KeyEventHandler(this, &frmConfig::frmConfig_KeyDown);
    else
        this->KeyDown -= gcnew System::Windows::Forms::KeyEventHandler(this, &frmConfig::frmConfig_KeyDown);
}

System::Void frmConfig::AdjustLocation() {
    //デスクトップ領域(タスクバー等除く)
    System::Drawing::Rectangle screen = System::Windows::Forms::Screen::GetWorkingArea(this);
    //現在のデスクトップ領域の座標
    Point CurrentDesktopLocation = this->DesktopLocation::get();
    //チェック開始
    bool ChangeLocation = false;
    if (CurrentDesktopLocation.X + this->Size.Width > screen.Width) {
        ChangeLocation = true;
        CurrentDesktopLocation.X = clamp(screen.X - this->Size.Width, 4, CurrentDesktopLocation.X);
    }
    if (CurrentDesktopLocation.Y + this->Size.Height > screen.Height) {
        ChangeLocation = true;
        CurrentDesktopLocation.Y = clamp(screen.Y - this->Size.Height, 4, CurrentDesktopLocation.Y);
    }
    if (ChangeLocation) {
        this->StartPosition = FormStartPosition::Manual;
        this->DesktopLocation::set(CurrentDesktopLocation);
    }
}

System::Void frmConfig::SetInputBufRange() {
    fcgNUInputBufSize->Minimum = QSV_INPUT_BUF_MIN;
    fcgNUInputBufSize->Maximum = QSV_INPUT_BUF_MAX;
}

System::Void frmConfig::UpdateMfxLibDetection() {
    UInt32 mfxlib_hw = featuresHW->GetmfxLibVer();
    UInt32 mfxlib_sw = featuresSW->GetmfxLibVer();
    fcgLBMFXLibDetectionHwValue->Text = (check_lib_version(mfxlib_hw, MFX_LIB_VERSION_1_1.Version)) ? 
        L"v" + ((mfxlib_hw>>16).ToString() + L"." + (mfxlib_hw & 0x0000ffff).ToString()) : L"-----";
    fcgLBMFXLibDetectionSwValue->Text = (check_lib_version(mfxlib_sw, MFX_LIB_VERSION_1_1.Version)) ? 
        L"v" + ((mfxlib_sw>>16).ToString() + L"." + (mfxlib_sw & 0x0000ffff).ToString()) : L"-----";
}

System::Void frmConfig::InitForm() {
    //CPU情報の取得
    getCPUInfoDelegate = gcnew SetCPUInfoDelegate(this, &frmConfig::SetCPUInfo);
    getCPUInfoDelegate->BeginInvoke(nullptr, nullptr);
    getGPUInfoDelegate = gcnew SetGPUInfoDelegate(this, &frmConfig::SetGPUInfo);
    getGPUInfoDelegate->BeginInvoke(nullptr, nullptr);
    //ローカル設定のロード
    LoadLocalStg();
    //ローカル設定の反映
    SetLocalStg();
    //設定ファイル集の初期化
    InitStgFileList();
    //コンボボックスの値を設定
    InitComboBox();
    //バッファサイズの最大最少をセット
    SetInputBufRange();
    //タイトル表示
    this->Text = String(AUO_FULL_NAME).ToString();
    //バージョン情報,コンパイル日時
    fcgLBVersion->Text     = Path::GetFileNameWithoutExtension(String(AUO_NAME_W).ToString()) + L" " + String(AUO_VERSION_STR).ToString() + L"  based on Intel(R) Media SDK " + String(MSDK_SAMPLE_VERSION).ToString();
    fcgLBVersionDate->Text = L"build " + String(__DATE__).ToString() + L" " + String(__TIME__).ToString();
    //ツールチップ
    SetHelpToolTips();
    //HWエンコードの可否
    UpdateMfxLibDetection();
    UInt32 mfxlib_hw = featuresHW->GetmfxLibVer();
    fcgCBHWEncode->Enabled = check_lib_version(mfxlib_hw, MFX_LIB_VERSION_1_1.Version) != 0;
    if (!fcgCBHWEncode->Enabled)
        fcgCBHWEncode->Checked = false;
    //パラメータセット
    ConfToFrm(conf);
    //イベントセット
    SetTXMaxLenAll(); //テキストボックスの最大文字数
    SetAllCheckChangedEvents(this); //変更の確認,ついでにNUのEnterEvent
#ifdef HIDE_MPEG2
    tabPageMpgMux = fcgtabControlMux->TabPages[2];
    fcgtabControlMux->TabPages->RemoveAt(2);
#endif
    //表示位置の調整
    AdjustLocation();
    //キー設定
    SetStgEscKey(sys_dat->exstg->s_local.enable_stg_esc_key != 0);
    //フォントの設定
    if (str_has_char(sys_dat->exstg->s_local.conf_font.name))
        SetFontFamilyToForm(this, gcnew FontFamily(String(sys_dat->exstg->s_local.conf_font.name).ToString()), this->Font->FontFamily);
    //フォームの変更可不可を更新
    fcgChangeMuxerVisible(nullptr, nullptr);
    EnableSettingsNoteChange(false);
    UpdateFeatures();
    fcgChangeEnabled(nullptr, nullptr);
    fcgCBHWEncode->CheckedChanged += gcnew System::EventHandler(this, &frmConfig::fcgCBHWLibChanged);
}

/////////////         データ <-> GUI     /////////////
System::Void frmConfig::ConfToFrm(CONF_GUIEX *cnf) {
    this->SuspendLayout();

    SetCXIndex(fcgCXOutputType,   get_cx_index(list_outtype, cnf->qsv.CodecId));
    SetCXIndex(fcgCXEncMode,      get_cx_index(list_encmode, cnf->qsv.nEncMode));
    SetCXIndex(fcgCXQuality,      get_cx_index(list_quality, cnf->qsv.nTargetUsage));
    SetNUValue(fcgNUBitrate,      cnf->qsv.nBitRate);
    SetNUValue(fcgNUMaxkbps,      cnf->qsv.nMaxBitrate);
    SetNUValue(fcgNUQPI,          cnf->qsv.nQPI);
    SetNUValue(fcgNUQPP,          cnf->qsv.nQPP);
    SetNUValue(fcgNUQPB,          cnf->qsv.nQPB);
    SetNUValue(fcgNUICQQuality,   cnf->qsv.nICQQuality);
    SetNUValue(fcgNUQVBR,         cnf->qsv.nQVBRQuality);
    SetNUValue(fcgNUGopLength,    Convert::ToDecimal(cnf->qsv.nGOPLength));
    SetNUValue(fcgNURef,          cnf->qsv.nRef);
    SetNUValue(fcgNUBframes,      cnf->qsv.nBframes);
    SetCXIndex(fcgCXTrellis,      get_cx_index(list_avc_trellis, cnf->qsv.nTrellis));
    SetCXIndex(fcgCXCodecLevel,   get_cx_index(list_avc_level, cnf->qsv.CodecLevel));
    SetCXIndex(fcgCXCodecProfile, get_cx_index(list_avc_profile, cnf->qsv.CodecProfile));
    if (fcgCBHWEncode->Enabled)
        fcgCBHWEncode->Checked  = cnf->qsv.bUseHWLib;
    if (fcgCBFixedFunc->Enabled)
        fcgCBFixedFunc->Checked = cnf->qsv.bUseFixedFunc != 0;
    if (fcgCBD3DMemAlloc->Enabled)
        fcgCBD3DMemAlloc->Checked = cnf->qsv.memType != SYSTEM_MEMORY;
    SetNUValue(fcgNUAVBRAccuarcy, cnf->qsv.nAVBRAccuarcy / Convert::ToDecimal(10.0));
    SetNUValue(fcgNUAVBRConvergence, cnf->qsv.nAVBRConvergence);
    SetNUValue(fcgNULookaheadDepth, cnf->qsv.nLookaheadDepth);
    fcgCBAdaptiveI->Checked     = cnf->qsv.bAdaptiveI != 0;
    fcgCBAdaptiveB->Checked     = cnf->qsv.bAdaptiveB != 0;
    fcgCBBPyramid->Checked      = cnf->qsv.bBPyramid != 0;
    SetCXIndex(fcgCXLookaheadDS,  get_cx_index(list_lookahead_ds, cnf->qsv.nLookaheadDS));
    fcgCBMBBRC->Checked         = cnf->qsv.bMBBRC != 0;
    fcgCBExtBRC->Checked        = cnf->qsv.bExtBRC != 0;
    SetNUValue(fcgNUWinBRCSize,       cnf->qsv.nWinBRCSize);
    SetCXIndex(fcgCXInterlaced,   get_cx_index(list_interlaced, cnf->qsv.nPicStruct));
    if (cnf->qsv.nPAR[0] * cnf->qsv.nPAR[1] <= 0)
        cnf->qsv.nPAR[0] = cnf->qsv.nPAR[1] = 0;
    SetCXIndex(fcgCXAspectRatio, (cnf->qsv.nPAR[0] < 0));
    SetNUValue(fcgNUAspectRatioX, abs(cnf->qsv.nPAR[0]));
    SetNUValue(fcgNUAspectRatioY, abs(cnf->qsv.nPAR[1]));
    fcgCBSceneChange->Checked    = false == cnf->qsv.bforceGOPSettings;
    fcgCBOpenGOP->Checked        = cnf->qsv.bopenGOP;

    SetNUValue(fcgNUSlices,       cnf->qsv.nSlices);

    fcgCBBlurayCompat->Checked   = cnf->qsv.nBluray != 0;

    SetNUValue(fcgNUQPMin,         cnf->qsv.nQPMin[0]);
    SetNUValue(fcgNUQPMax,         cnf->qsv.nQPMax[0]);

    fcgCBCABAC->Checked          = !cnf->qsv.bCAVLC;
    fcgCBRDO->Checked            = cnf->qsv.bRDO;
    SetNUValue(fcgNUMVSearchWindow, cnf->qsv.MVSearchWindow.x);
    SetCXIndex(fcgCXMVPred,      get_cx_index(list_mv_presicion,    cnf->qsv.nMVPrecision));
    SetCXIndex(fcgCXInterPred,   get_cx_index(list_pred_block_size, cnf->qsv.nInterPred));
    SetCXIndex(fcgCXIntraPred,   get_cx_index(list_pred_block_size, cnf->qsv.nIntraPred));

    fcgCBDirectBiasAdjust->Checked = 0 != cnf->qsv.bDirectBiasAdjust;
    SetCXIndex(fcgCXMVCostScaling, (cnf->qsv.bGlobalMotionAdjust) ? get_cx_index(list_mv_cost_scaling, cnf->qsv.nMVCostScaling) : 0);

    fcgCBDeblock->Checked        = cnf->qsv.bNoDeblock == 0;
    fcgCBIntraRefresh->Checked   = cnf->qsv.bIntraRefresh != 0;

    SetCXIndex(fcgCXTransfer,    get_cx_index(list_transfer, cnf->qsv.Transfer));
    SetCXIndex(fcgCXColorMatrix, get_cx_index(list_colormatrix, cnf->qsv.ColorMatrix));
    SetCXIndex(fcgCXColorPrim,   get_cx_index(list_colorprim,   cnf->qsv.ColorPrim));
    SetCXIndex(fcgCXVideoFormat, get_cx_index(list_videoformat, cnf->qsv.VideoFormat));
    fcgCBFullrange->Checked      = cnf->qsv.bFullrange;

    //Vpp
    fcgCBUseVpp->Checked                   = cnf->qsv.vpp.bEnable;
    fcgCBVppResize->Checked                = cnf->qsv.vpp.bUseResize;
    SetNUValue(fcgNUVppResizeW,              cnf->qsv.nDstWidth);
    SetNUValue(fcgNUVppResizeH,              cnf->qsv.nDstHeight);
    fcgCBVppDenoise->Checked               = cnf->qsv.vpp.bUseDenoise;
    SetNUValue(fcgNUVppDenoise,              cnf->qsv.vpp.nDenoise);
    fcgCBVppDetail->Checked                = cnf->qsv.vpp.bUseDetailEnhance;
    SetNUValue(fcgNUVppDetail,               cnf->qsv.vpp.nDetailEnhance);
    SetCXIndex(fcgCXDeinterlace,             cnf->qsv.vpp.nDeinterlace);
    SetCXIndex(fcgCXTelecinePatterns,        get_cx_index(list_telecine_patterns, cnf->qsv.vpp.nTelecinePattern));
    SetCXIndex(fcgCXImageStabilizer,         cnf->qsv.vpp.nImageStabilizer);
    SetCXIndex(fcgCXFPSConversion,           cnf->qsv.vpp.nFPSConversion);

        //SetCXIndex(fcgCXX264Priority,        cnf->vid.priority);
        const bool enable_tc2mp4_muxer = (0 != str_has_char(sys_dat->exstg->s_mux[MUXER_TC2MP4].base_cmd));
        SetCXIndex(fcgCXTempDir,             cnf->oth.temp_dir);
        SetNUValue(fcgNUInputBufSize,        cnf->qsv.nInputBufSize);
        fcgCBAFS->Checked                  = (enable_tc2mp4_muxer) ? cnf->vid.afs != 0 : false;
        fcgCBAuoTcfileout->Checked         = (enable_tc2mp4_muxer) ? cnf->vid.auo_tcfile_out != 0 : false;

        //音声
        fcgCBAudioOnly->Checked            = cnf->oth.out_audio_only != 0;
        fcgCBFAWCheck->Checked             = cnf->aud.faw_check != 0;
        SetCXIndex(fcgCXAudioEncoder,        cnf->aud.encoder);
        fcgCBAudio2pass->Checked           = cnf->aud.use_2pass != 0;
        fcgCBAudioUsePipe->Checked = (CurrentPipeEnabled && !cnf->aud.use_wav);
        SetCXIndex(fcgCXAudioDelayCut,       cnf->aud.delay_cut);
        SetCXIndex(fcgCXAudioEncMode,        cnf->aud.enc_mode);
        SetNUValue(fcgNUAudioBitrate,       (cnf->aud.bitrate != 0) ? cnf->aud.bitrate : GetCurrentAudioDefaultBitrate());
        SetCXIndex(fcgCXAudioPriority,       cnf->aud.priority);
        SetCXIndex(fcgCXAudioTempDir,        cnf->aud.aud_temp_dir);
        SetCXIndex(fcgCXAudioEncTiming,      cnf->aud.audio_encode_timing);
        fcgCBRunBatBeforeAudio->Checked    =(cnf->oth.run_bat & RUN_BAT_BEFORE_AUDIO) != 0;
        fcgCBRunBatAfterAudio->Checked     =(cnf->oth.run_bat & RUN_BAT_AFTER_AUDIO) != 0;
        fcgTXBatBeforeAudioPath->Text      = String(cnf->oth.batfile.before_audio).ToString();
        fcgTXBatAfterAudioPath->Text       = String(cnf->oth.batfile.after_audio).ToString();

        //mux
        fcgCBMP4MuxerExt->Checked          = cnf->mux.disable_mp4ext == 0;
        fcgCBMP4MuxApple->Checked          = cnf->mux.apple_mode != 0;
        SetCXIndex(fcgCXMP4CmdEx,            cnf->mux.mp4_mode);
        SetCXIndex(fcgCXMP4BoxTempDir,       cnf->mux.mp4_temp_dir);
        fcgCBMKVMuxerExt->Checked          = cnf->mux.disable_mkvext == 0;
        SetCXIndex(fcgCXMKVCmdEx,            cnf->mux.mkv_mode);
        fcgCBMPGMuxerExt->Checked          = cnf->mux.disable_mpgext == 0;
        SetCXIndex(fcgCXMPGCmdEx,            cnf->mux.mpg_mode);
        SetCXIndex(fcgCXMuxPriority,         cnf->mux.priority);

        fcgCBRunBatBefore->Checked         =(cnf->oth.run_bat & RUN_BAT_BEFORE_PROCESS) != 0;
        fcgCBRunBatAfter->Checked          =(cnf->oth.run_bat & RUN_BAT_AFTER_PROCESS)  != 0;
        fcgCBWaitForBatBefore->Checked     =(cnf->oth.dont_wait_bat_fin & RUN_BAT_BEFORE_PROCESS) == 0;
        fcgCBWaitForBatAfter->Checked      =(cnf->oth.dont_wait_bat_fin & RUN_BAT_AFTER_PROCESS)  == 0;
        fcgTXBatBeforePath->Text           = String(cnf->oth.batfile.before_process).ToString();
        fcgTXBatAfterPath->Text            = String(cnf->oth.batfile.after_process).ToString();

        SetfcgTSLSettingsNotes(cnf->oth.notes);

    this->ResumeLayout();
    this->PerformLayout();
}

System::Void frmConfig::FrmToConf(CONF_GUIEX *cnf) {
    //これもひたすら書くだけ。めんどい
    cnf->qsv.CodecId                = list_outtype[fcgCXOutputType->SelectedIndex].value;
    cnf->qsv.nEncMode               = (mfxU16)list_encmode[fcgCXEncMode->SelectedIndex].value;
    cnf->qsv.nTargetUsage           = (mfxU16)list_quality[fcgCXQuality->SelectedIndex].value;
    cnf->qsv.CodecProfile           = (mfxU16)list_avc_profile[fcgCXCodecProfile->SelectedIndex].value;
    cnf->qsv.CodecLevel             = (mfxU16)list_avc_level[fcgCXCodecLevel->SelectedIndex].value;
    cnf->qsv.nBitRate               = (mfxU16)fcgNUBitrate->Value;
    cnf->qsv.nMaxBitrate            = (mfxU16)fcgNUMaxkbps->Value;
    cnf->qsv.nLookaheadDepth        = (mfxU16)fcgNULookaheadDepth->Value;
    cnf->qsv.nRef                   = (mfxU16)fcgNURef->Value;
    cnf->qsv.bopenGOP               = fcgCBOpenGOP->Checked;
    cnf->qsv.bforceGOPSettings      = fcgCBSceneChange->Checked == 0;
    cnf->qsv.nGOPLength             = (mfxU16)fcgNUGopLength->Value;
    cnf->qsv.nQPI                   = (mfxU16)fcgNUQPI->Value;
    cnf->qsv.nQPP                   = (mfxU16)fcgNUQPP->Value;
    cnf->qsv.nQPB                   = (mfxU16)fcgNUQPB->Value;
    cnf->qsv.nICQQuality            = (mfxU16)fcgNUICQQuality->Value;
    cnf->qsv.nQVBRQuality           = (mfxU16)fcgNUQVBR->Value;
    cnf->qsv.nBframes               = (mfxI16)fcgNUBframes->Value;
    cnf->qsv.nTrellis               = (mfxU16)list_avc_trellis[fcgCXTrellis->SelectedIndex].value;
    cnf->qsv.nPicStruct             = (mfxU16)list_interlaced[fcgCXInterlaced->SelectedIndex].value;
    cnf->qsv.bAdaptiveI             = fcgCBAdaptiveI->Checked;
    cnf->qsv.bAdaptiveB             = fcgCBAdaptiveB->Checked;
    cnf->qsv.bBPyramid              = fcgCBBPyramid->Checked;
    cnf->qsv.nLookaheadDS           = (mfxU16)list_lookahead_ds[fcgCXLookaheadDS->SelectedIndex].value;
    cnf->qsv.bMBBRC                 = fcgCBMBBRC->Checked;
    cnf->qsv.bExtBRC                = fcgCBExtBRC->Checked;
    cnf->qsv.nWinBRCSize            = (mfxU16)fcgNUWinBRCSize->Value;
    cnf->qsv.bUseHWLib              = fcgCBHWEncode->Checked;
    cnf->qsv.bUseFixedFunc          = fcgCBFixedFunc->Checked;
    cnf->qsv.memType                = (mfxU8)((fcgCBD3DMemAlloc->Checked) ? HW_MEMORY : SYSTEM_MEMORY);
    cnf->qsv.nAVBRAccuarcy          = (mfxU16)(fcgNUAVBRAccuarcy->Value * 10);
    cnf->qsv.nAVBRConvergence       = (mfxU16)fcgNUAVBRConvergence->Value;
    cnf->qsv.nSlices                = (mfxU16)fcgNUSlices->Value;
    cnf->qsv.nQPMin[0]              = (mfxU8)fcgNUQPMin->Value;
    cnf->qsv.nQPMin[1]              = (mfxU8)fcgNUQPMin->Value;
    cnf->qsv.nQPMin[2]              = (mfxU8)fcgNUQPMin->Value;
    cnf->qsv.nQPMax[0]              = (mfxU8)fcgNUQPMax->Value;
    cnf->qsv.nQPMax[1]              = (mfxU8)fcgNUQPMax->Value;
    cnf->qsv.nQPMax[2]              = (mfxU8)fcgNUQPMax->Value;

    cnf->qsv.nBluray                = fcgCBBlurayCompat->Checked;

    cnf->qsv.bNoDeblock             = !fcgCBDeblock->Checked;
    cnf->qsv.bIntraRefresh          = fcgCBIntraRefresh->Checked;

    cnf->qsv.bCAVLC                 = !fcgCBCABAC->Checked;
    cnf->qsv.bRDO                   = fcgCBRDO->Checked;
    cnf->qsv.MVSearchWindow.x       = (mfxI16)fcgNUMVSearchWindow->Value;
    cnf->qsv.MVSearchWindow.y       = (mfxI16)fcgNUMVSearchWindow->Value;
    cnf->qsv.nMVPrecision           = (mfxU16)list_mv_presicion[fcgCXMVPred->SelectedIndex].value;
    cnf->qsv.nInterPred             = (mfxU16)list_pred_block_size[fcgCXInterPred->SelectedIndex].value;
    cnf->qsv.nIntraPred             = (mfxU16)list_pred_block_size[fcgCXIntraPred->SelectedIndex].value;

    cnf->qsv.bDirectBiasAdjust      = fcgCBDirectBiasAdjust->Checked;
    cnf->qsv.bGlobalMotionAdjust    = list_mv_cost_scaling[fcgCXMVCostScaling->SelectedIndex].value > 0;
    cnf->qsv.nMVCostScaling         = (mfxU8)((cnf->qsv.bGlobalMotionAdjust) ? list_mv_cost_scaling[fcgCXMVCostScaling->SelectedIndex].value : 0);

    cnf->qsv.ColorMatrix            = (mfxU16)list_colormatrix[fcgCXColorMatrix->SelectedIndex].value;
    cnf->qsv.ColorPrim              = (mfxU16)list_colorprim[fcgCXColorPrim->SelectedIndex].value;
    cnf->qsv.Transfer               = (mfxU16)list_transfer[fcgCXTransfer->SelectedIndex].value;
    cnf->qsv.VideoFormat            = (mfxU16)list_videoformat[fcgCXVideoFormat->SelectedIndex].value;
    cnf->qsv.bFullrange             = fcgCBFullrange->Checked;


    cnf->qsv.nPAR[0]                = (int)fcgNUAspectRatioX->Value;
    cnf->qsv.nPAR[1]                = (int)fcgNUAspectRatioY->Value;
    if (fcgCXAspectRatio->SelectedIndex == 1) {
        cnf->qsv.nPAR[0] *= -1;
        cnf->qsv.nPAR[1] *= -1;
    }

    //vpp
    cnf->qsv.vpp.bEnable             = fcgCBUseVpp->Checked;
    cnf->qsv.vpp.bUseResize          = fcgCBVppResize->Checked;
    cnf->qsv.nDstWidth               = (mfxU16)fcgNUVppResizeW->Value;
    cnf->qsv.nDstHeight              = (mfxU16)fcgNUVppResizeH->Value;
    cnf->qsv.vpp.bUseDenoise         = fcgCBVppDenoise->Checked;
    cnf->qsv.vpp.nDenoise            = (mfxU16)fcgNUVppDenoise->Value;
    cnf->qsv.vpp.bUseDetailEnhance   = fcgCBVppDetail->Checked;
    cnf->qsv.vpp.nDetailEnhance      = (mfxU16)fcgNUVppDetail->Value;
    cnf->qsv.vpp.nDeinterlace        = (mfxU16)list_deinterlace_ja[fcgCXDeinterlace->SelectedIndex].value;
    cnf->qsv.vpp.nTelecinePattern    = (mfxU16)list_telecine_patterns[fcgCXTelecinePatterns->SelectedIndex].value;
    cnf->qsv.vpp.nImageStabilizer    = (mfxU16)list_vpp_image_stabilizer[fcgCXImageStabilizer->SelectedIndex].value;
    cnf->qsv.vpp.nFPSConversion      = (mfxU16)list_vpp_fps_conversion[fcgCXFPSConversion->SelectedIndex].value;

    //拡張部
    const bool enable_tc2mp4_muxer = (0 != str_has_char(sys_dat->exstg->s_mux[MUXER_TC2MP4].base_cmd));
    cnf->oth.temp_dir               = fcgCXTempDir->SelectedIndex;
    cnf->qsv.nInputBufSize          = (mfxU16)fcgNUInputBufSize->Value;
    cnf->vid.auo_tcfile_out         = (enable_tc2mp4_muxer) ? fcgCBAuoTcfileout->Checked : false;
    cnf->vid.afs                    = (enable_tc2mp4_muxer) ? fcgCBAFS->Checked : false;

    //音声部
    cnf->aud.encoder                = fcgCXAudioEncoder->SelectedIndex;
    cnf->oth.out_audio_only         = fcgCBAudioOnly->Checked;
    cnf->aud.faw_check              = fcgCBFAWCheck->Checked;
    cnf->aud.enc_mode               = fcgCXAudioEncMode->SelectedIndex;
    cnf->aud.bitrate                = (int)fcgNUAudioBitrate->Value;
    cnf->aud.use_2pass              = fcgCBAudio2pass->Checked;
    cnf->aud.use_wav                = !fcgCBAudioUsePipe->Checked;
    cnf->aud.delay_cut              = fcgCXAudioDelayCut->SelectedIndex;
    cnf->aud.priority               = fcgCXAudioPriority->SelectedIndex;
    cnf->aud.audio_encode_timing    = fcgCXAudioEncTiming->SelectedIndex;
    cnf->aud.aud_temp_dir           = fcgCXAudioTempDir->SelectedIndex;

    //mux部
    cnf->mux.disable_mp4ext         = !fcgCBMP4MuxerExt->Checked;
    cnf->mux.apple_mode             = fcgCBMP4MuxApple->Checked;
    cnf->mux.mp4_mode               = fcgCXMP4CmdEx->SelectedIndex;
    cnf->mux.mp4_temp_dir           = fcgCXMP4BoxTempDir->SelectedIndex;
    cnf->mux.disable_mkvext         = !fcgCBMKVMuxerExt->Checked;
    cnf->mux.mkv_mode               = fcgCXMKVCmdEx->SelectedIndex;
    cnf->mux.disable_mpgext         = !fcgCBMPGMuxerExt->Checked;
    cnf->mux.mpg_mode               = fcgCXMPGCmdEx->SelectedIndex;
    cnf->mux.priority               = fcgCXMuxPriority->SelectedIndex;
    
    cnf->oth.run_bat                = RUN_BAT_NONE;
    cnf->oth.run_bat               |= (fcgCBRunBatBeforeAudio->Checked) ? RUN_BAT_BEFORE_AUDIO   : NULL;
    cnf->oth.run_bat               |= (fcgCBRunBatAfterAudio->Checked)  ? RUN_BAT_AFTER_AUDIO    : NULL;
    cnf->oth.run_bat               |= (fcgCBRunBatBefore->Checked)      ? RUN_BAT_BEFORE_PROCESS : NULL;
    cnf->oth.run_bat               |= (fcgCBRunBatAfter->Checked)       ? RUN_BAT_AFTER_PROCESS  : NULL;
    cnf->oth.dont_wait_bat_fin      = RUN_BAT_NONE;
    cnf->oth.dont_wait_bat_fin     |= (!fcgCBWaitForBatBefore->Checked) ? RUN_BAT_BEFORE_PROCESS : NULL;
    cnf->oth.dont_wait_bat_fin     |= (!fcgCBWaitForBatAfter->Checked)  ? RUN_BAT_AFTER_PROCESS  : NULL;
    GetCHARfromString(cnf->oth.batfile.before_process, sizeof(cnf->oth.batfile.before_process), fcgTXBatBeforePath->Text);
    GetCHARfromString(cnf->oth.batfile.after_process,  sizeof(cnf->oth.batfile.after_process),  fcgTXBatAfterPath->Text);
    GetCHARfromString(cnf->oth.batfile.before_audio, sizeof(cnf->oth.batfile.before_audio), fcgTXBatBeforeAudioPath->Text);
    GetCHARfromString(cnf->oth.batfile.after_audio,  sizeof(cnf->oth.batfile.after_audio),  fcgTXBatAfterAudioPath->Text);

    GetfcgTSLSettingsNotes(cnf->oth.notes, sizeof(cnf->oth.notes));
}

System::Void frmConfig::GetfcgTSLSettingsNotes(char *notes, int nSize) {
    ZeroMemory(notes, nSize);
    if (fcgTSLSettingsNotes->ForeColor == Color::FromArgb(StgNotesColor[0][0], StgNotesColor[0][1], StgNotesColor[0][2]))
        GetCHARfromString(notes, nSize, fcgTSLSettingsNotes->Text);
}

System::Void frmConfig::SetfcgTSLSettingsNotes(const char *notes) {
    if (str_has_char(notes)) {
        fcgTSLSettingsNotes->ForeColor = Color::FromArgb(StgNotesColor[0][0], StgNotesColor[0][1], StgNotesColor[0][2]);
        fcgTSLSettingsNotes->Text = String(notes).ToString();
    } else {
        fcgTSLSettingsNotes->ForeColor = Color::FromArgb(StgNotesColor[1][0], StgNotesColor[1][1], StgNotesColor[1][2]);
        fcgTSLSettingsNotes->Text = String(DefaultStgNotes).ToString();
    }
}

System::Void frmConfig::SetfcgTSLSettingsNotes(String^ notes) {
    if (notes->Length && String::Compare(notes, String(DefaultStgNotes).ToString()) != 0) {
        fcgTSLSettingsNotes->ForeColor = Color::FromArgb(StgNotesColor[0][0], StgNotesColor[0][1], StgNotesColor[0][2]);
        fcgTSLSettingsNotes->Text = notes;
    } else {
        fcgTSLSettingsNotes->ForeColor = Color::FromArgb(StgNotesColor[1][0], StgNotesColor[1][1], StgNotesColor[1][2]);
        fcgTSLSettingsNotes->Text = String(DefaultStgNotes).ToString();
    }
}

System::Void frmConfig::SetChangedEvent(Control^ control, System::EventHandler^ _event) {
    System::Type^ ControlType = control->GetType();
    if (ControlType == NumericUpDown::typeid)
        ((NumericUpDown^)control)->ValueChanged += _event;
    else if (ControlType == ComboBox::typeid)
        ((ComboBox^)control)->SelectedIndexChanged += _event;
    else if (ControlType == CheckBox::typeid)
        ((CheckBox^)control)->CheckedChanged += _event;
    else if (ControlType == TextBox::typeid)
        ((TextBox^)control)->TextChanged += _event;
}

System::Void frmConfig::SetToolStripEvents(ToolStrip^ TS, System::Windows::Forms::MouseEventHandler^ _event) {
    for (int i = 0; i < TS->Items->Count; i++) {
        ToolStripButton^ TSB = dynamic_cast<ToolStripButton^>(TS->Items[i]);
        if (TSB != nullptr) TSB->MouseDown += _event;
    }
}

System::Void frmConfig::SetAllCheckChangedEvents(Control ^top) {
    //再帰を使用してすべてのコントロールのtagを調べ、イベントをセットする
    for (int i = 0; i < top->Controls->Count; i++) {
        System::Type^ type = top->Controls[i]->GetType();
        if (type == NumericUpDown::typeid)
            top->Controls[i]->Enter += gcnew System::EventHandler(this, &frmConfig::NUSelectAll);

        if (type == Label::typeid || type == Button::typeid)
            ;
        else if (type == ToolStrip::typeid)
            SetToolStripEvents((ToolStrip^)(top->Controls[i]), gcnew System::Windows::Forms::MouseEventHandler(this, &frmConfig::fcgTSItem_MouseDown));
        else if (top->Controls[i]->Tag == nullptr)
            SetAllCheckChangedEvents(top->Controls[i]);
        else if (String::Equals(top->Controls[i]->Tag->ToString(), L"chValue"))
            SetChangedEvent(top->Controls[i], gcnew System::EventHandler(this, &frmConfig::CheckOtherChanges));
        else
            SetAllCheckChangedEvents(top->Controls[i]);
    }
}

System::Void frmConfig::SetHelpToolTips() {

    //拡張
    fcgTTEx->SetToolTip(fcgCXTempDir,      L""
        + L"一時ファイル群\n"
        + L"・音声一時ファイル(wav / エンコード後音声)\n"
        + L"・動画一時ファイル\n"
        + L"・タイムコードファイル\n"
        + L"・qpファイル\n"
        + L"・mux後ファイル\n"
        + L"の作成場所を指定します。"
        );
    fcgTTEx->SetToolTip(fcgCBAFS,                L""
        + L"自動フィールドシフト(afs)を使用してVFR化を行います。\n"
        + L"エンコード時にタイムコードを作成し、mux時に埋め込んで\n"
        + L"フレームレートを変更します。\n"
        + L"\n"
        + L"あとからフレームレートを変更するため、\n"
        + L"ビットレート設定が正確に反映されなくなる点に注意してください。"
        );
    fcgTTEx->SetToolTip(fcgCBAuoTcfileout, L""
        + L"タイムコードを出力します。このタイムコードは\n"
        + L"自動フィールドシフト(afs)を反映したものになります。"
        );

    //音声
    fcgTTEx->SetToolTip(fcgCXAudioEncoder, L""
        + L"使用する音声エンコーダを指定します。\n"
        + L"これらの設定はQSVEnc.iniに記述されています。"
        );
    fcgTTEx->SetToolTip(fcgCBAudioOnly,    L""
        + L"動画の出力を行わず、音声エンコードのみ行います。\n"
        + L"音声エンコードに失敗した場合などに使用してください。"
        );
    fcgTTEx->SetToolTip(fcgCBFAWCheck,     L""
        + L"音声エンコード時に音声がFakeAACWav(FAW)かどうかの判定を行い、\n"
        + L"FAWだと判定された場合、設定を無視して、\n"
        + L"自動的にFAWを使用するよう切り替えます。\n"
        + L"\n"
        + L"一度音声エンコーダからFAW(fawcl)を選択し、\n"
        + L"実行ファイルの場所を指定しておく必要があります。"
        );
    fcgTTEx->SetToolTip(fcgBTAudioEncoderPath, L""
        + L"音声エンコーダの場所を指定します。\n"
        + L"\n"
        + L"この設定はx264guiEx.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    fcgTTEx->SetToolTip(fcgCXAudioEncMode, L""
        + L"音声エンコーダのエンコードモードを切り替えます。\n"
        + L"これらの設定はQSVEnc.iniに記述されています。"
        );
    fcgTTEx->SetToolTip(fcgCBAudio2pass,   L""
        + L"音声エンコードを2passで行います。\n"
        + L"2pass時はパイプ処理は行えません。"
        );
    fcgTTEx->SetToolTip(fcgCBAudioUsePipe, L""
        + L"パイプを通して、音声データをエンコーダに渡します。\n"
        + L"パイプと2passは同時に指定できません。"
        );
    fcgTTEx->SetToolTip(fcgNUAudioBitrate, L""
        + L"音声ビットレートを指定します。"
        );
    fcgTTEx->SetToolTip(fcgCXAudioPriority, L""
        + L"音声エンコーダのCPU優先度を設定します。\n"
        + L"AviutlSync で Aviutlの優先度と同じになります。"
        );
    fcgTTEx->SetToolTip(fcgCXAudioEncTiming, L""
        + L"音声を処理するタイミングを設定します。\n"
        + L" 後　 … 映像→音声の順で処理します。\n"
        + L" 前　 … 音声→映像の順で処理します。\n"
        + L" 同時 … 映像と音声を同時に処理します。"
        );
    fcgTTEx->SetToolTip(fcgCXAudioTempDir, L""
        + L"音声一時ファイル(エンコード後のファイル)\n"
        + L"の出力先を変更します。"
        );
    fcgTTEx->SetToolTip(fcgBTCustomAudioTempDir, L""
        + L"音声一時ファイルの場所を「カスタム」にした時に\n"
        + L"使用される音声一時ファイルの場所を指定します。\n"
        + L"\n"
        + L"この設定はx264guiEx.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    //音声バッチファイル実行
    fcgTTEx->SetToolTip(fcgCBRunBatBeforeAudio, L""
        + L"音声エンコード開始前にバッチファイルを実行します。"
        );
    fcgTTEx->SetToolTip(fcgCBRunBatAfterAudio, L""
        + L"音声エンコード終了後、バッチファイルを実行します。"
        );
    fcgTTEx->SetToolTip(fcgBTBatBeforeAudioPath, L""
        + L"音声エンコード終了後実行するバッチファイルを指定します。\n"
        + L"実際のバッチ実行時には新たに\"<バッチファイル名>_tmp.bat\"を作成、\n"
        + L"指定したバッチファイルの内容をコピーし、\n"
        + L"さらに特定文字列を置換して実行します。\n"
        + L"使用できる置換文字列はreadmeをご覧下さい。"
        );
    fcgTTEx->SetToolTip(fcgBTBatAfterAudioPath, L""
        + L"音声エンコード終了後実行するバッチファイルを指定します。\n"
        + L"実際のバッチ実行時には新たに\"<バッチファイル名>_tmp.bat\"を作成、\n"
        + L"指定したバッチファイルの内容をコピーし、\n"
        + L"さらに特定文字列を置換して実行します。\n"
        + L"使用できる置換文字列はreadmeをご覧下さい。"
        );

    //muxer
    fcgTTEx->SetToolTip(fcgCBMP4MuxerExt, L""
        + L"指定したmuxerでmuxを行います。\n"
        + L"チェックを外すとmuxを行いません。"
        );
    fcgTTEx->SetToolTip(fcgCXMP4CmdEx,    L""
        + L"muxerに渡す追加オプションを選択します。\n"
        + L"これらの設定はQSVEnc.iniに記述されています。"
        );
    fcgTTEx->SetToolTip(fcgBTMP4MuxerPath, L""
        + L"mp4用muxerの場所を指定します。\n"
        + L"\n"
        + L"この設定はQSVEnc.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    fcgTTEx->SetToolTip(fcgBTMP4RawPath, L""
        + L"raw用mp4muxerの場所を指定します。\n"
        + L"\n"
        + L"この設定はQSVEnc.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    fcgTTEx->SetToolTip(fcgCXMP4BoxTempDir, L""
        + L"mp4box用の一時フォルダの場所を指定します。"
        );
    fcgTTEx->SetToolTip(fcgBTMP4BoxTempDir, L""
        + L"mp4box用一時フォルダの場所を「カスタム」に設定した際に\n"
        + L"使用される一時フォルダの場所です。\n"
        + L"\n"
        + L"この設定はQSVEnc.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    fcgTTEx->SetToolTip(fcgCBMKVMuxerExt, L""
        + L"指定したmuxerでmuxを行います。\n"
        + L"チェックを外すとmuxを行いません。"
        );
    fcgTTEx->SetToolTip(fcgCXMKVCmdEx,    L""
        + L"muxerに渡す追加オプションを選択します。\n"
        + L"これらの設定はQSVEnc.iniに記述されています。"
        );
    fcgTTEx->SetToolTip(fcgBTMKVMuxerPath, L""
        + L"mkv用muxerの場所を指定します。\n"
        + L"\n"
        + L"この設定はQSVEnc.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    fcgTTEx->SetToolTip(fcgCBMPGMuxerExt, L""
        + L"指定したmuxerでmuxを行います。\n"
        + L"チェックを外すとmuxを行いません。"
        );
    fcgTTEx->SetToolTip(fcgCXMPGCmdEx,    L""
        + L"muxerに渡す追加オプションを選択します。\n"
        + L"これらの設定はQSVEnc.iniに記述されています。"
        );
    fcgTTEx->SetToolTip(fcgBTMPGMuxerPath, L""
        + L"mpg用muxerの場所を指定します。\n"
        + L"\n"
        + L"この設定はQSVEnc.confに保存され、\n"
        + L"バッチ処理ごとの変更はできません。"
        );
    fcgTTEx->SetToolTip(fcgCXMuxPriority, L""
        + L"muxerのCPU優先度を指定します。\n"
        + L"AviutlSync で Aviutlの優先度と同じになります。"
        );
    //バッチファイル実行
    fcgTTEx->SetToolTip(fcgCBRunBatBefore, L""
        + L"エンコード開始前にバッチファイルを実行します。"
        );
    fcgTTEx->SetToolTip(fcgCBRunBatAfter, L""
        + L"エンコード終了後、バッチファイルを実行します。"
        );
    fcgTTEx->SetToolTip(fcgCBWaitForBatBefore, L""
        + L"バッチ処理開始後、バッチ処理が終了するまで待機します。"
        );
    fcgTTEx->SetToolTip(fcgCBWaitForBatAfter, L""
        + L"バッチ処理開始後、バッチ処理が終了するまで待機します。"
        );
    fcgTTEx->SetToolTip(fcgBTBatBeforePath, L""
        + L"エンコード終了後実行するバッチファイルを指定します。\n"
        + L"実際のバッチ実行時には新たに\"<バッチファイル名>_tmp.bat\"を作成、\n"
        + L"指定したバッチファイルの内容をコピーし、\n"
        + L"さらに特定文字列を置換して実行します。\n"
        + L"使用できる置換文字列はreadmeをご覧下さい。"
        );
    fcgTTEx->SetToolTip(fcgBTBatAfterPath, L""
        + L"エンコード終了後実行するバッチファイルを指定します。\n"
        + L"実際のバッチ実行時には新たに\"<バッチファイル名>_tmp.bat\"を作成、\n"
        + L"指定したバッチファイルの内容をコピーし、\n"
        + L"さらに特定文字列を置換して実行します。\n"
        + L"使用できる置換文字列はreadmeをご覧下さい。"
        );
    //上部ツールストリップ
    fcgTSBDelete->ToolTipText = L""
        + L"現在選択中のプロファイルを削除します。";

    fcgTSBOtherSettings->ToolTipText = L""
        + L"プロファイルの保存フォルダを変更します。";

    fcgTSBSave->ToolTipText = L""
        + L"現在の設定をプロファイルに上書き保存します。";

    fcgTSBSaveNew->ToolTipText = L""
        + L"現在の設定を新たなプロファイルに保存します。";

    //他
    fcgTTEx->SetToolTip(fcgTXCmd,         L""
        + L"x264に渡される予定のコマンドラインです。\n"
        + L"エンコード時には更に\n"
        + L"・「追加コマンド」の付加\n"
        + L"・\"auto\"な設定項目の反映\n"
        + L"・必要な情報の付加(--fps/-o/--input-res/--input-csp/--frames等)\n"
        + L"が行われます。\n"
        + L"\n"
        + L"このウィンドウはダブルクリックで拡大縮小できます。"
        );
    fcgTTEx->SetToolTip(fcgBTDefault,     L""
        + L"デフォルト設定をロードします。"
        );
}
System::Void frmConfig::ShowExehelp(String^ ExePath, String^ args) {
    if (!File::Exists(ExePath)) {
        MessageBox::Show(L"指定された実行ファイルが存在しません。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
    } else {
        char exe_path[MAX_PATH_LEN];
        char file_path[MAX_PATH_LEN];
        char cmd[MAX_CMD_LEN];
        GetCHARfromString(exe_path, sizeof(exe_path), ExePath);
        apply_appendix(file_path, _countof(file_path), exe_path, "_fullhelp.txt");
        File::Delete(String(file_path).ToString());
        array<String^>^ arg_list = args->Split(L';');
        for (int i = 0; i < arg_list->Length; i++) {
            if (i) {
                StreamWriter^ sw;
                try {
                    sw = gcnew StreamWriter(String(file_path).ToString(), true, System::Text::Encoding::GetEncoding("shift_jis"));
                    sw->WriteLine();
                    sw->WriteLine();
                } catch (...) {
                    //ファイルオープンに失敗…初回のget_exe_message_to_fileでエラーとなるため、おそらく起こらない
                } finally {
                    if (sw != nullptr) { sw->Close(); }
                }
            }
            GetCHARfromString(cmd, sizeof(cmd), arg_list[i]);
            if (get_exe_message_to_file(exe_path, cmd, file_path, AUO_PIPE_MUXED, 5) != RP_SUCCESS) {
                File::Delete(String(file_path).ToString());
                MessageBox::Show(L"helpの取得に失敗しました。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
                return;
            }
        }
        try {
            System::Diagnostics::Process::Start(String(file_path).ToString());
        } catch (...) {
            MessageBox::Show(L"helpを開く際に不明なエラーが発生しました。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
        }
    }
}

System::Void frmConfig::UpdateFeatures() {
    //表示更新
    const mfxU32 codecId = list_outtype[fcgCXOutputType->SelectedIndex].value;
    const mfxU32 currentLib = (fcgCBHWEncode->Checked) ? featuresHW->GetmfxLibVer() : featuresSW->GetmfxLibVer();
    const bool currentLibValid = 0 != check_lib_version(currentLib, MFX_LIB_VERSION_1_1.Version);
    String^ currentAPI = ((fcgCBHWEncode->Checked) ? L"hw" : L"sw") + L": ";
    currentAPI += (currentLibValid) ? L"API v" + ((currentLib>>16).ToString() + L"." + (currentLib & 0x0000ffff).ToString()) : L"-------";
    fcgLBFeaturesCurrentAPIVer->Text = currentAPI + L" / codec: " + String(list_outtype[fcgCXOutputType->SelectedIndex].desc).ToString();

    auto dataGridViewFont = gcnew System::Drawing::Font(L"Meiryo UI", 8.25F, FontStyle::Regular, GraphicsUnit::Point, static_cast<Byte>(128));

    fcgDGVFeatures->ReadOnly = true;
    fcgDGVFeatures->AllowUserToAddRows = false;
    fcgDGVFeatures->AllowUserToResizeRows = false;
    fcgDGVFeatures->AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode::Fill;

    fcgDGVFeatures->DataSource = (fcgCBHWEncode->Checked) ? featuresHW->getFeatureTable(codecId) : featuresSW->getFeatureTable(codecId); //テーブルをバインド

    fcgDGVFeatures->Columns[0]->FillWeight = 240;
    fcgDGVFeatures->DefaultCellStyle->Font = dataGridViewFont;
    fcgDGVFeatures->ColumnHeadersDefaultCellStyle->Font = dataGridViewFont;
    fcgDGVFeatures->RowHeadersDefaultCellStyle->Font = dataGridViewFont;

    fcgCheckVppFeatures();
}

System::Void frmConfig::SaveQSVFeature() {
    //WinXPにおいて、OpenFileDialogはCurrentDirctoryを勝手に変更しやがるので、
    //一度保存し、あとから再適用する
    String^ CurrentDir = Directory::GetCurrentDirectory();

    if (nullptr == saveFileQSVFeautures) {
        saveFileQSVFeautures = gcnew SaveFileDialog();
        WCHAR aviutl_dir[MAX_PATH_LEN];
        get_aviutl_dir(aviutl_dir, _countof(aviutl_dir));
        saveFileQSVFeautures->InitialDirectory = String(aviutl_dir).ToString();
    }
    saveFileQSVFeautures->FileName = L"";

    //ofd->Filter = L"pngファイル(*.png)|*.png|txtファイル(*.txt)|*.txt|csvファイル(*.csv)|*.csv";
    saveFileQSVFeautures->Filter = L"pngファイル(*.png)|*.png";

    saveFileQSVFeautures->Title = L"保存するファイル名を入力してください";
    if (System::Windows::Forms::DialogResult::OK == saveFileQSVFeautures->ShowDialog()) {
        String^ SavePath = saveFileQSVFeautures->FileName;
        saveFileQSVFeautures->InitialDirectory = Path::GetDirectoryName(saveFileQSVFeautures->FileName);

        bool isImage = 0 == String::Compare(".png", Path::GetExtension(SavePath), true);

        if (isImage) {
            SaveQSVFeatureAsImg(SavePath);
        } else {
            //SaveQSVFeatureAsTxt(SavePath);
        }
    }
    
    Directory::SetCurrentDirectory(CurrentDir);
}

System::Void frmConfig::SaveQSVFeatureAsImg(String^ SavePath) {
    bool SaveAsBmp = 0 == String::Compare(".bmp", Path::GetExtension(SavePath), true);
    Bitmap^ bmp = gcnew Bitmap(tabPageFeatures->Width, tabPageFeatures->Height);
    try {
        tabPageFeatures->DrawToBitmap(bmp, System::Drawing::Rectangle(0, 0, tabPageFeatures->Width, tabPageFeatures->Height));
        bmp->Save(SavePath, (SaveAsBmp) ? System::Drawing::Imaging::ImageFormat::Bmp : System::Drawing::Imaging::ImageFormat::Png);
    } catch (...) {
        MessageBox::Show(L"画像の保存中にエラーが発生しました。", L"エラー", MessageBoxButtons::OK, MessageBoxIcon::Error);
    } finally {
        delete bmp;
    }
}
System::Void frmConfig::SaveQSVFeatureAsTxt(String^ SavePath) {

}
System::Void frmConfig::SetCPUInfo() {
    //CPU名
    if (nullptr == StrCPUInfo || StrCPUInfo->Length <= 0) {
        TCHAR cpu_info[256];
        getCPUInfo(cpu_info, _countof(cpu_info));
        StrCPUInfo = String(cpu_info).ToString()->Replace(L" CPU ", L" ");
    }
    if (this->InvokeRequired) {
        SetCPUInfoDelegate^ sl = gcnew SetCPUInfoDelegate(this, &frmConfig::SetCPUInfo);
        this->Invoke(sl);
    } else {
        fcgLBCPUInfoOnFeatureTab->Text = StrCPUInfo;
    }
}
System::Void frmConfig::SetGPUInfo() {
    //GPU名
    if (nullptr == StrGPUInfo || StrGPUInfo->Length <= 0) {
        TCHAR gpu_info[256];
        getGPUInfo(_T("Intel"), gpu_info, _countof(gpu_info));
        StrGPUInfo = String(gpu_info).ToString();
    }
    if (this->InvokeRequired) {
        SetGPUInfoDelegate^ sl = gcnew SetGPUInfoDelegate(this, &frmConfig::SetGPUInfo);
        this->Invoke(sl);
    } else {
        fcgLBGPUInfoOnFeatureTab->Text = StrGPUInfo;
    }
}

#pragma warning( pop )

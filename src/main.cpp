#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include "gdr/gdr.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

using namespace geode::prelude;

struct Macro : public gdr::Replay<Macro, gdr::Input> {
    Macro() : Replay("dim5lBOT", "v1.0.0") {}
};

struct GlobalState {
    Macro macro;
    bool recording = false;
    bool playing = false;
    bool isMacroInput = false;
    int frame = 0;
    size_t playIndex = 0;
};

static GlobalState g;

std::filesystem::path getMacroDir() {
    auto path = Mod::get()->getSaveDir() / "macros";
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path getTempPath() {
    return Mod::get()->getSaveDir() / "temp.gdr";
}

void saveMacro(std::filesystem::path path) {
    auto data = g.macro.exportData();
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
}

bool loadMacro(std::filesystem::path path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    g.macro = Macro::importData(data);
    std::sort(g.macro.inputs.begin(), g.macro.inputs.end());
    return !g.macro.inputs.empty();
}

// ===== GJBaseGameLayer Hook =====
class $modify(MyGJBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool hold, int button, bool player2) {
        GJBaseGameLayer::handleButton(hold, button, player2);
        if (g.isMacroInput) return;
        if (!g.recording) return;
        if (!m_player1 || m_player1->m_isDead) return;
        g.macro.inputs.push_back(gdr::Input(g.frame, button, player2, hold));
    }
};

// ===== PlayLayer Hook =====
class $modify(MyPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g.frame = 0;
        g.playIndex = 0;
    }

    void update(float dt) {
        // xdBot 방식: update 전에 재생 입력 주입
        if (g.playing && !m_player1->m_isDead && !m_levelEndAnimationStarted) {
            while (g.playIndex < g.macro.inputs.size() &&
                   (int)g.macro.inputs[g.playIndex].frame == g.frame) {
                auto& input = g.macro.inputs[g.playIndex];
                g.isMacroInput = true;
                GJBaseGameLayer::handleButton(input.down, input.button, input.player2);
                g.isMacroInput = false;
                g.playIndex++;
            }
        }

        PlayLayer::update(dt);

        if (g.recording || g.playing) g.frame++;
    }
};

// ===== Save Name Layer =====
class SaveNameLayer : public CCLayer {
public:
    TextInput* nameInput = nullptr;
    bool saved = false; // 중복 저장 방지

    static SaveNameLayer* create() {
        auto ret = new SaveNameLayer();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }

    bool init() {
        if (!CCLayer::init()) return false;
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto dimBg = CCLayerColor::create({0, 0, 0, 150});
        dimBg->setContentSize(winSize);
        addChild(dimBg);

        auto panel = CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({280, 160});
        panel->setPosition(winSize / 2);
        addChild(panel);

        auto title = CCLabelBMFont::create("Save Macro", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 60);
        title->setScale(0.7f);
        addChild(title);

        nameInput = TextInput::create(200, "Enter filename...");
        nameInput->setPosition(winSize / 2);
        addChild(nameInput);

        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(SaveNameLayer::onSave));
        auto cancelBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Cancel", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(SaveNameLayer::onCancel));

        auto menu = CCMenu::create(saveBtn, cancelBtn, nullptr);
        menu->alignItemsHorizontallyWithPadding(10);
        menu->setPosition(winSize.width / 2, winSize.height / 2 - 50);
        addChild(menu);
        return true;
    }

    void onSave(CCObject*) {
        if (saved) return; // 중복 클릭 방지
        auto name = nameInput->getString();
        if (name.empty()) {
            FLAlertLayer::create("dim5lBOT", "Enter a filename!", "OK")->show();
            return;
        }
        saved = true;
        auto path = getMacroDir() / (name + ".gdr");
        saveMacro(path);
        FLAlertLayer::create("dim5lBOT", ("Saved: " + name + ".gdr").c_str(), "OK")->show();
        removeFromParentAndCleanup(true);
    }

    void onCancel(CCObject*) { removeFromParentAndCleanup(true); }
};

// ===== Load List Layer =====
class LoadListLayer : public CCLayer {
public:
    static LoadListLayer* create() {
        auto ret = new LoadListLayer();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }

    bool init() {
        if (!CCLayer::init()) return false;
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto dimBg = CCLayerColor::create({0, 0, 0, 150});
        dimBg->setContentSize(winSize);
        addChild(dimBg);

        auto panel = CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({300, 280});
        panel->setPosition(winSize / 2);
        addChild(panel);

        auto title = CCLabelBMFont::create("Load Macro", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 120);
        title->setScale(0.7f);
        addChild(title);

        auto dir = getMacroDir();
        float y = winSize.height / 2 + 70;
        bool any = false;

        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            auto path = entry.path();
            if (path.extension() != ".gdr") continue;
            any = true;
            auto name = path.stem().string();
            auto btn = CCMenuItemSpriteExtra::create(
                ButtonSprite::create(name.c_str(), "chatFont.fnt", "GJ_button_04.png"),
                this, menu_selector(LoadListLayer::onLoadFile));
            btn->setUserObject(CCString::create(path.string()));
            btn->setScale(0.8f);
            auto menu = CCMenu::create(btn, nullptr);
            menu->setPosition(winSize.width / 2, y);
            addChild(menu);
            y -= 45;
        }

        if (!any) {
            auto label = CCLabelBMFont::create("No macros found!", "chatFont.fnt");
            label->setPosition(winSize / 2);
            label->setScale(0.7f);
            addChild(label);
        }

        auto closeBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_05.png"),
            this, menu_selector(LoadListLayer::onClose));
        auto closeMenu = CCMenu::create(closeBtn, nullptr);
        closeMenu->setPosition(winSize.width / 2, winSize.height / 2 - 110);
        addChild(closeMenu);
        return true;
    }

    void onLoadFile(CCObject* sender) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto pathStr = static_cast<CCString*>(item->getUserObject())->getCString();
        if (loadMacro(pathStr)) {
            FLAlertLayer::create("dim5lBOT",
                fmt::format("Loaded {} inputs!", g.macro.inputs.size()).c_str(), "OK")->show();
        } else {
            FLAlertLayer::create("dim5lBOT", "Failed to load!", "OK")->show();
        }
        removeFromParentAndCleanup(true);
    }

    void onClose(CCObject*) { removeFromParentAndCleanup(true); }
};

// ===== Bot Menu =====
class BotMenuLayer : public CCLayer {
public:
    CCLabelBMFont* statusLabel = nullptr;

    static BotMenuLayer* create() {
        auto ret = new BotMenuLayer();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }

    bool init() {
        if (!CCLayer::init()) return false;
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto dimBg = CCLayerColor::create({0, 0, 0, 120});
        dimBg->setContentSize(winSize);
        addChild(dimBg);

        auto panel = CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({300, 370});
        panel->setPosition(winSize / 2);
        addChild(panel);

        auto title = CCLabelBMFont::create("dim5lBOT", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 155);
        title->setScale(0.9f);
        addChild(title);

        statusLabel = CCLabelBMFont::create("Idle", "chatFont.fnt");
        statusLabel->setPosition(winSize.width / 2, winSize.height / 2 + 127);
        statusLabel->setScale(0.6f);
        statusLabel->setColor({255, 255, 100});
        addChild(statusLabel);
        updateStatus();

        auto recBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Record", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(BotMenuLayer::onRecord));
        auto playBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Play", "goldFont.fnt", "GJ_button_02.png"),
            this, menu_selector(BotMenuLayer::onPlay));
        auto stopBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Stop", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(BotMenuLayer::onStop));
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_03.png"),
            this, menu_selector(BotMenuLayer::onSave));
        auto loadBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Load", "goldFont.fnt", "GJ_button_04.png"),
            this, menu_selector(BotMenuLayer::onLoad));
        auto debugBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Debug", "goldFont.fnt", "GJ_button_04.png"),
            this, menu_selector(BotMenuLayer::onDebug));
        auto closeBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_05.png"),
            this, menu_selector(BotMenuLayer::onClose));

        auto menu = CCMenu::create(recBtn, playBtn, stopBtn, saveBtn, loadBtn, debugBtn, closeBtn, nullptr);
        menu->alignItemsVerticallyWithPadding(5);
        menu->setPosition(winSize / 2);
        addChild(menu);
        return true;
    }

    void updateStatus() {
        if (!statusLabel) return;
        if (g.recording) statusLabel->setString("Recording...");
        else if (g.playing) statusLabel->setString("Playing...");
        else if (!g.macro.inputs.empty())
            statusLabel->setString(fmt::format("Loaded ({} inputs)", g.macro.inputs.size()).c_str());
        else statusLabel->setString("Idle - No macro");
    }

    void onRecord(CCObject*) {
        g.macro.inputs.clear();
        g.recording = true;
        g.playing = false;
        g.frame = 0;
        g.playIndex = 0;
        removeFromParentAndCleanup(true);
    }

    void onPlay(CCObject*) {
        if (g.macro.inputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "No macro loaded!", "OK")->show();
            return;
        }
        g.recording = false;
        g.playing = true;
        g.playIndex = 0;
        g.frame = 0;
        if (auto pl = PlayLayer::get()) pl->resetLevelFromStart();
        removeFromParentAndCleanup(true);
    }

    void onStop(CCObject*) {
        bool wasRecording = g.recording;
        g.recording = false;
        g.playing = false;
        if (wasRecording && !g.macro.inputs.empty()) {
            saveMacro(getTempPath());
            FLAlertLayer::create("dim5lBOT",
                fmt::format("Stopped! {} inputs.\nUse Save to name it.", g.macro.inputs.size()).c_str(),
                "OK")->show();
        } else {
            FLAlertLayer::create("dim5lBOT", "Stopped!", "OK")->show();
        }
        updateStatus();
    }

    void onSave(CCObject*) {
        if (!std::filesystem::exists(getTempPath())) {
            FLAlertLayer::create("dim5lBOT", "No temp! Record first.", "OK")->show();
            return;
        }
        // BotMenuLayer를 먼저 닫고 SaveNameLayer 열기
        auto parent = getParent();
        removeFromParentAndCleanup(true);
        if (parent) parent->addChild(SaveNameLayer::create(), 200);
    }

    void onLoad(CCObject*) {
        auto parent = getParent();
        removeFromParentAndCleanup(true);
        if (parent) parent->addChild(LoadListLayer::create(), 200);
    }

    void onDebug(CCObject*) {
        auto msg = fmt::format(
            "SaveDir:\n{}\nTemp: {}\nInputs: {}\nFrame: {}\nRec: {} Play: {}",
            Mod::get()->getSaveDir().string(),
            std::filesystem::exists(getTempPath()) ? "YES" : "NO",
            g.macro.inputs.size(),
            g.frame,
            g.recording,
            g.playing
        );
        FLAlertLayer::create("Debug", msg.c_str(), "OK")->show();
    }

    void onClose(CCObject*) { removeFromParentAndCleanup(true); }
};

void openBotMenu(CCNode* parent) {
    parent->addChild(BotMenuLayer::create(), 100);
}

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto botBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("BOT", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(MyPauseLayer::onBotMenu));
        botBtn->setScale(0.8f);
        auto menu = CCMenu::create(botBtn, nullptr);
        menu->setPosition(CCPoint(70, 200));
        addChild(menu, 10);
    }
    void onBotMenu(CCObject*) { openBotMenu(this); }
};

class $modify(MyEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        auto botBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("BOT", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(MyEndLevelLayer::onBotMenu));
        botBtn->setScale(0.8f);
        auto menu = CCMenu::create(botBtn, nullptr);
        menu->setPosition(CCPoint(70, 200));
        addChild(menu, 10);
    }
    void onBotMenu(CCObject*) { openBotMenu(this); }
};

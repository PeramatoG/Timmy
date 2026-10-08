/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_recognition_guidance.h"
#include "grammar-parser.h"
#include "timmy/command/command_processor.h"
#include "timmy/core/command_recognition_profile.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using timmy::asr::RecognitionHints;
    using timmy::asr::RecognitionConstraintIntent;
    using timmy::asr::detail::makeWhisperRecognitionGuidance;

    // These checks remain enabled in Release builds.
    void require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::vector<std::uint32_t> codepoints(std::string_view text)
    {
        std::vector<std::uint32_t> result;
        for (std::size_t offset = 0; offset < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[offset++]);
            std::uint32_t value = first;
            std::size_t continuation = 0;
            std::uint32_t minimum = 0;
            if (first >= 0xc2 && first <= 0xdf)
            { value = first & 0x1f; continuation = 1; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef)
            { value = first & 0x0f; continuation = 2; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4)
            { value = first & 0x07; continuation = 3; minimum = 0x10000; }
            else require(first < 0x80, "Test text contains invalid UTF-8");
            require(offset + continuation <= text.size(), "Test text contains truncated UTF-8");
            for (std::size_t index = 0; index < continuation; ++index)
            {
                const auto byte = static_cast<unsigned char>(text[offset++]);
                require((byte & 0xc0) == 0x80, "Test text contains invalid UTF-8 continuation");
                value = (value << 6) | (byte & 0x3f);
            }
            require(value >= minimum && value <= 0x10ffff
                && !(value >= 0xd800 && value <= 0xdfff), "Test text contains invalid Unicode scalar");
            result.push_back(value);
        }
        return result;
    }

    // Interpret the pinned grammar-parser's output directly. Matching complete
    // Unicode strings requires no Whisper context, tokens, model or inference.
    class ParsedGrammar
    {
    public:
        explicit ParsedGrammar(const timmy::asr::detail::WhisperRecognitionGuidance& guidance)
            : parsed_(grammar_parser::parse(guidance.grammar.c_str()))
        {
            require(!parsed_.rules.empty(), "Generated grammar did not parse");
            require(parsed_.symbol_ids.contains(guidance.startRule), "Generated grammar lacks its start rule");
            start_ = parsed_.symbol_ids.at(guidance.startRule);
        }

        bool accepts(std::string_view text)
        {
            input_ = codepoints(text);
            memo_.clear();
            active_.clear();
            return matchRule(start_, 0).contains(input_.size());
        }

    private:
        using Positions = std::set<std::size_t>;
        using Key = std::pair<std::uint32_t, std::size_t>;

        Positions matchRule(std::uint32_t ruleId, std::size_t position)
        {
            const Key key{ruleId, position};
            if (const auto found = memo_.find(key); found != memo_.end()) return found->second;
            require(active_.insert(key).second, "Generated grammar has non-consuming recursion");
            require(ruleId < parsed_.rules.size(), "Generated grammar references an undefined rule");
            const auto& rule = parsed_.rules[ruleId];
            Positions ends;
            Positions current{position};
            for (std::size_t index = 0; index < rule.size();)
            {
                const auto element = rule[index];
                if (element.type == WHISPER_GRETYPE_ALT || element.type == WHISPER_GRETYPE_END)
                {
                    ends.insert(current.begin(), current.end());
                    if (element.type == WHISPER_GRETYPE_END) break;
                    current = {position};
                    ++index;
                    continue;
                }
                Positions next;
                if (element.type == WHISPER_GRETYPE_RULE_REF)
                {
                    for (const auto offset : current)
                    {
                        const auto matched = matchRule(element.value, offset);
                        next.insert(matched.begin(), matched.end());
                    }
                    ++index;
                }
                else
                {
                    require(element.type == WHISPER_GRETYPE_CHAR
                        || element.type == WHISPER_GRETYPE_CHAR_NOT, "Unsupported parsed grammar element");
                    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
                    do
                    {
                        const auto lower = rule[index++].value;
                        auto upper = lower;
                        if (index < rule.size() && rule[index].type == WHISPER_GRETYPE_CHAR_RNG_UPPER)
                            upper = rule[index++].value;
                        ranges.emplace_back(lower, upper);
                    } while (index < rule.size() && rule[index].type == WHISPER_GRETYPE_CHAR_ALT);
                    for (const auto offset : current)
                    {
                        if (offset >= input_.size()) continue;
                        bool matched = false;
                        for (const auto& [lower, upper] : ranges)
                            matched |= input_[offset] >= lower && input_[offset] <= upper;
                        if (matched != (element.type == WHISPER_GRETYPE_CHAR_NOT)) next.insert(offset + 1);
                    }
                }
                current = std::move(next);
            }
            active_.erase(key);
            memo_.emplace(key, ends);
            return ends;
        }

        grammar_parser::parse_state parsed_;
        std::uint32_t start_{};
        std::vector<std::uint32_t> input_;
        std::map<Key, Positions> memo_;
        std::set<Key> active_;
    };

    RecognitionHints commandHints(std::vector<std::string> vocabulary)
    {
        return {std::move(vocabulary), {}, RecognitionConstraintIntent::commandPhrase};
    }

    void selectedProfileVocabulary()
    {
        for (const auto& profile : {timmy::command::CommandProfile{}, timmy::command::hydra2CommandProfile()})
        {
            const auto hints = timmy::core::commandRecognitionHints(profile);
            const auto guidance = makeWhisperRecognitionGuidance(hints);
            require(timmy::asr::detail::isValidWhisperRecognitionGuidance(guidance), "Profile grammar is invalid");
            require(guidance.grammarPenalty == 20.0F, "Existing grammar penalty changed");
            require(guidance.grammar.find("letter ::=") == std::string::npos
                && guidance.grammar.find("word ::= letter") == std::string::npos, "Unrestricted word production remains");
            ParsedGrammar grammar{guidance};
            for (const auto& entry : hints.vocabulary)
                require(grammar.accepts(" " + entry), "Supplied vocabulary was rejected: " + entry);
            for (const auto entry : {"añade", "súmale", "selección", "vacía", "but not"})
                require(grammar.accepts(std::string{" "} + entry), "Spoken alias was rejected: " + std::string{entry});
            for (const auto number : {"1", "15", "105", "550", "0001", "12345678901234567890"})
                require(grammar.accepts(std::string{" "} + number), "Decimal integer was rejected");
            for (const auto phrase : {" %", " 50%", " 50 %", " channel 1,3", " channel 1, 3",
                    " channel 1 , 3", " channel 1  at 50", " channel 1 at 50.",
                    " channel 1 at 50!", " channel 1 at 50?"})
                require(grammar.accepts(phrase), "Required lexical syntax was rejected: " + std::string{phrase});
            for (const auto word : {"botijo", "camión", "bicicleta", "elefante", "channelchannel", "channelsx"})
            {
                require(!grammar.accepts(std::string{" "} + word), "Unprovided vocabulary was accepted: " + std::string{word});
                require(!grammar.accepts(std::string{" channel 1 at 50 "} + word), "Unprovided word was accepted in a phrase");
                require(guidance.grammar.find(word) == std::string::npos, "Unprovided word appears in generated grammar");
            }
            for (const auto phrase : {"channel 1 at 50", "", " ", " channel1", " channel 1 at 50;", " channel 1 at 50!!"})
                require(!grammar.accepts(phrase), "Unsupported lexical syntax was accepted: " + std::string{phrase});
        }
    }

    void exactEntriesAndEscaping()
    {
        const std::vector<std::string> entries{"but not", "añade", "súmale", "selección", "vacía",
            "say \"hello\"", "path\\name", "[a-z]", "x|y", "repeat*", "group(name)", "#alias", "a ::= b",
            "\xf0\x9f\x94\x86"};
        ParsedGrammar grammar{makeWhisperRecognitionGuidance(commandHints(entries))};
        for (const auto& entry : entries)
            require(grammar.accepts(" " + entry), "Literal was not escaped or preserved: " + entry);
        for (const auto entry : {"but", "not", "but 1 not", "butnot", "a", "z", "x", "y",
                "repeat", "groupname", "say hello", "pathname", "Añade", "anade"})
            require(!grammar.accepts(std::string{" "} + entry), "An exact vocabulary entry was expanded: " + std::string{entry});

        auto selected = timmy::command::CommandProfile{};
        selected.recognition.terms = {"beacon", "but not"};
        ParsedGrammar selectedGrammar{makeWhisperRecognitionGuidance(timmy::core::commandRecognitionHints(selected))};
        require(selectedGrammar.accepts(" beacon") && selectedGrammar.accepts(" but not")
            && !selectedGrammar.accepts(" channel") && !selectedGrammar.accepts(" but"),
            "Grammar did not follow the selected profile's vocabulary");
    }

    void invalidEntriesAndCompactPrompt()
    {
        std::vector<std::string> invalid{"", " ", "   ", " leading", "trailing ", "\t", "a\nb", "a\rb",
            std::string{"bad\0entry", 9}, std::string{"bad\x01"}, std::string{"bad\x7f"},
            std::string{"\xc0\xaf"}, std::string{"\xc3"}, std::string{"\x80"}, std::string{"\xe2\x28\xa1"},
            std::string{"\xed\xa0\x80"}, std::string{"\xf4\x90\x80\x80"}};
        const auto empty = makeWhisperRecognitionGuidance(commandHints({}));
        for (const auto& entry : invalid)
        {
            const auto guidance = makeWhisperRecognitionGuidance(commandHints({entry}));
            require(guidance.grammar == empty.grammar, "Invalid entry changed the numeric-only grammar");
            ParsedGrammar grammar{guidance};
            require(grammar.accepts(" 550%") && grammar.accepts(" 1, 15!")
                && !grammar.accepts(" invalid") && !grammar.accepts(" "), "Invalid hints broke the fallback grammar");
        }
        auto mixed = commandHints(invalid);
        mixed.vocabulary.push_back("selección");
        ParsedGrammar mixedGrammar{makeWhisperRecognitionGuidance(mixed)};
        require(mixedGrammar.accepts(" selección") && !mixedGrammar.accepts(" leading")
            && !mixedGrammar.accepts(" trailing"), "Invalid entries contaminated a valid vocabulary");

        auto hints = timmy::core::commandRecognitionHints();
        const auto phrases = hints.expectedPhrases;
        const auto original = makeWhisperRecognitionGuidance(hints);
        require(!original.initialPrompt.empty() && original.initialPrompt.size() <= 256, "Whisper prompt is not compact");
        (void)codepoints(original.initialPrompt);
        hints.expectedPhrases.assign(100, "This representative phrase must never enter the inference prompt.");
        const auto changed = makeWhisperRecognitionGuidance(hints);
        require(changed.initialPrompt == original.initialPrompt && changed.grammar == original.grammar,
            "Representative phrases changed Whisper inference guidance");
        require(!phrases.empty() && timmy::core::commandRecognitionHints().expectedPhrases == phrases,
            "Backend-neutral representative phrases were removed or mutated");
        hints.vocabulary.assign(100, "selección");
        const auto repeated = makeWhisperRecognitionGuidance(hints);
        require(repeated.initialPrompt.size() <= 256, "Repeated vocabulary overflowed the prompt budget");
        (void)codepoints(repeated.initialPrompt);
        const std::string largeEntry(242, 'x');
        const auto nearlyFull = makeWhisperRecognitionGuidance(commandHints({largeEntry}));
        const auto overflow = makeWhisperRecognitionGuidance(commandHints({largeEntry, "short"}));
        require(nearlyFull.initialPrompt.size() == 255 && overflow.initialPrompt == nearlyFull.initialPrompt,
            "A nearly full prompt overflowed when appending another vocabulary entry");
        const std::string oversizedEntry(300, 'x');
        const auto oversized = makeWhisperRecognitionGuidance(commandHints({oversizedEntry, "small"}));
        require(oversized.initialPrompt.size() <= 256
            && oversized.initialPrompt.find(oversizedEntry) == std::string::npos
            && oversized.initialPrompt.find("small") != std::string::npos,
            "An oversized vocabulary entry was not skipped as a whole in the prompt");
        ParsedGrammar oversizedGrammar{oversized};
        require(oversizedGrammar.accepts(" " + oversizedEntry) && oversizedGrammar.accepts(" small"),
            "Prompt limits truncated the grammar vocabulary");

        std::vector<std::string> uniqueEntries;
        for (int index = 0; index < 100; ++index) uniqueEntries.push_back("term" + std::to_string(index));
        const auto many = makeWhisperRecognitionGuidance(commandHints(uniqueEntries));
        require(!many.initialPrompt.empty() && many.initialPrompt.size() <= 256
            && many.initialPrompt.find(uniqueEntries.back()) == std::string::npos,
            "Growing unique vocabulary was not bounded in the prompt");
        ParsedGrammar manyGrammar{many};
        require(manyGrammar.accepts(" " + uniqueEntries.back()), "Prompt entry limits reduced the grammar vocabulary");
    }

    void semanticParserRemainsAuthoritative()
    {
        const auto profile = timmy::command::CommandProfile{};
        ParsedGrammar grammar{makeWhisperRecognitionGuidance(timmy::core::commandRecognitionHints(profile))};
        const timmy::command::CommandProcessor processor{profile};
        for (const auto phrase : {"channel group 5", "channel 5 at 105", "clear selection all"})
        {
            require(grammar.accepts(std::string{" "} + phrase), "Vocabulary grammar incorrectly imposed semantic constraints");
            const auto result = processor.process(phrase);
            require(!result.parsed.command && !result.rendering.text,
                "A vocabulary-valid, semantically invalid phrase produced a command");
        }
    }

} // namespace

int main()
{
    try
    {
        selectedProfileVocabulary();
        exactEntriesAndEscaping();
        invalidEntriesAndCompactPrompt();
        semanticParserRemainsAuthoritative();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

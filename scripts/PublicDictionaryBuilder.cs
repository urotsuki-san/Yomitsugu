// Used by the Windows PowerShell 5.1 updater. No Python or extra runtime required.
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;

public static class YomitsuguDictionaryBuilder {
    public sealed class Result {
        public string Text;
        public int Readings, Entries, Symbols, ComputingTerms, LexicalEntries;
    }
    sealed class Entry { public string Word, Pos; }
    static readonly Regex Kana = new Regex("^[ぁ-ゖー]{2,30}$");
    static readonly Regex Annotation = new Regex(@"\([^)]*\)");
    static readonly Regex Flags = new Regex(@"\(([^)]*)\)");
    static readonly Regex Head = new Regex(@"^(.+?) \[([^]]+)\]$");
    static readonly Regex Noun = new Regex(@"\(n(?:[,)-]|$)");
    static readonly Regex Loanword = new Regex("^[ァ-ヶー・]{2,30}$");
    static readonly Regex Irregular = new Regex(@"\((?:ik|iK|io|oK)\)");
    static readonly Regex Obsolete = new Regex(@"\((?:arch|obs|obsc)\)");
    static readonly Regex ComputingWord = new Regex(@"^[A-Z][A-Z0-9+._-]{1,30}$");
    static readonly Regex GlossFlags = new Regex(@"\([^)]*\)|\{[^}]*\}");
    static string Hiragana(string value) {
        var chars=value.ToCharArray();
        for(int i=0;i<chars.Length;i++) if(chars[i]>='ァ' && chars[i]<='ヶ') chars[i]=(char)(chars[i]-0x60);
        return new string(chars);
    }
    static bool Add(SortedDictionary<string,List<Entry>> rows,string reading,string word,string pos) {
        if(!Kana.IsMatch(reading) || String.IsNullOrEmpty(word) || word.Length>48 || word.Contains("\uFFFD")) return false;
        List<Entry> values;
        if(!rows.TryGetValue(reading,out values)) { values=new List<Entry>(); rows.Add(reading,values); }
        foreach(var value in values) if(value.Word==word) {
            if(value.Pos=="補助語" && pos!="補助語") value.Pos=pos;
            return false;
        }
        if(values.Count>=32) return false;
        values.Add(new Entry { Word=word,Pos=pos }); return true;
    }
    static int AddLexical(SortedDictionary<string,List<Entry>> rows,string head,string glosses) {
        var match=Head.Match(head);
        var forms=(match.Success?match.Groups[1].Value:head).Split(';');
        var readings=match.Success?match.Groups[2].Value.Split(';'):forms;
        bool common=head.Contains("(P)") || glosses.Contains("(P)"), noun=Noun.IsMatch(glosses);
        int count=0;
        foreach(var form in forms) {
            string word=Annotation.Replace(form,"").Trim();
            if(!Loanword.IsMatch(word) && !common && !noun) continue;
            if(Irregular.IsMatch(form) || (Obsolete.IsMatch(glosses) && !common)) continue;
            foreach(var annotated in readings) {
                bool invalid=false, restricted=false, permitted=false;
                foreach(Match flagMatch in Flags.Matches(annotated)) {
                    string flag=flagMatch.Groups[1].Value;
                    if(flag=="ik" || flag=="ok" || flag=="gikun") invalid=true;
                    if(flag!="P") {
                        restricted=true;
                        foreach(var allowed in flag.Split(',')) if(allowed==word) permitted=true;
                    }
                }
                if(invalid || (restricted && !permitted)) continue;
                string reading=Hiragana(Annotation.Replace(annotated,"").Trim());
                if(Add(rows,reading,word,"補助語")) count++;
            }
        }
        return count;
    }
    public static Result Build(string glossary,string symbols) {
        var result=new Result();
        var rows=new SortedDictionary<string,List<Entry>>(StringComparer.Ordinal);
        foreach(var line in glossary.Split('\n')) {
            int split=line.IndexOf(" /",StringComparison.Ordinal);
            if(split<0) continue;
            string head=line.Substring(0,split),glosses=line.Substring(split+2);
            result.LexicalEntries+=AddLexical(rows,head,glosses);
            if(!line.Contains("{comp}")) continue;
            var match=Regex.Match(head,@"\[([^]]+)\]");
            string reading=Hiragana(match.Success?match.Groups[1].Value:head.Split(';')[0].Trim());
            if(!Kana.IsMatch(reading)) continue;
            foreach(var gloss in glosses.Split('/')) {
                string word=GlossFlags.Replace(gloss,"").Trim();
                if(ComputingWord.IsMatch(word) && Add(rows,reading,word,"名詞")) result.ComputingTerms++;
            }
        }
        bool first=true;
        foreach(var line in symbols.TrimStart('\uFEFF').Split('\n')) {
            if(first) { first=false; continue; }
            var columns=line.TrimEnd('\r').Split('\t');
            if(columns.Length<3) continue;
            foreach(var reading in Regex.Split(columns[2],@"\s+"))
                if(Add(rows,reading,columns[1],"記号")) result.Symbols++;
        }
        result.Symbols=result.ComputingTerms=result.LexicalEntries=0;
        foreach(var pair in rows) foreach(var value in pair.Value) {
            if(value.Pos=="記号") result.Symbols++;
            else if(value.Pos=="名詞") result.ComputingTerms++;
            else result.LexicalEntries++;
        }
        if(result.Symbols<500 || result.Symbols>20000 || result.ComputingTerms<10 || result.ComputingTerms>10000 ||
           result.LexicalEntries<20000 || result.LexicalEntries>650000) throw new InvalidOperationException("Source coverage changed.");
        foreach(var pair in new[]{new[]{"りーどみー","README"},new[]{"やじるし","→"},new[]{"まる","○"}}) {
            List<Entry> values; bool found=false;
            if(rows.TryGetValue(pair[0],out values)) foreach(var value in values) if(value.Word==pair[1]) found=true;
            if(!found) throw new InvalidOperationException("Required entry missing: "+pair[0]);
        }
        var output=new StringBuilder("# Sources: Mozc symbol.tsv (BSD-3-Clause); EDRDG EDICT2 (CC BY-SA 4.0)\n");
        foreach(var pair in rows) {
            // Stable priority: abbreviations/symbols before supplemental spellings.
            for(int pass=0;pass<2;pass++) foreach(var value in pair.Value) {
                if((value.Pos=="補助語") != (pass==1)) continue;
                output.Append(pair.Key).Append('\t').Append(value.Word).Append('\t').Append(value.Pos).Append('\n');
                result.Entries++;
            }
        }
        result.Text=output.ToString(); result.Readings=rows.Count;
        return result;
    }
}

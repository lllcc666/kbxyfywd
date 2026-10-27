#pragma once

// 嵌入的HTML内容 - 多个原始字符串字面量拼接
const char* g_html_content = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WinUI3 Style Tabs</title>
    <script>
        window.onerror = function(msg, url, line, col, error) {
            return true;
        };
    </script>
    <style>
        
        :root {
            
            --color-primary: #0078d4;
            --color-primary-hover: #106ebe;
            --color-primary-pressed: #005a9e;
            --color-text-primary: #1d1d1d;
            --color-text-secondary: #494949;
            --color-text-tertiary: #6e6e6e;
            --color-background: #ffffff;
            --color-background-secondary: #f2f2f2;
            --color-background-hover: #f7f7f7;
            --color-border: #e0e0e0;
            --color-border-hover: #c8c8c8;
            --color-border-focus: #0078d4;
            
            
            --radius-sm: 3px;
            --radius-md: 6px;
            --radius-lg: 8px;
   )HTML" \
R"HTML(         --radius-full: 9999px;
            
            
            --shadow-sm: 0 1px 3px rgba(0, 0, 0, 0.06);
            --shadow-md: 0 2px 8px rgba(0, 0, 0, 0.1);
            --shadow-lg: 0 4px 16px rgba(0, 0, 0, 0.12);
            --shadow-xl: 0 8px 24px rgba(0, 0, 0, 0.15);
            --shadow-elevated: 0 4px 12px rgba(0, 0, 0, 0.08), 0 8px 24px rgba(0, 0, 0, 0.08);
            
            
            --spacing-xs: 3px;
            --spacing-sm: 6px;
            --spacing-md: 10px;
            --spacing-lg: 14px;
            --spacing-xl: 18px;
            
            
            --easing-standard: cubic-bezier(0.4, 0, 0.2, 1);      
            --easing-enter: cubic-bezier(0, 0, 0.2, 1);           
            --easing-exit: cubic-bezier(0.4, 0, 1, 1);            
            --easing-bounce: cubic-bezier(0.34, 1.56, 0.64, 1);   
            --easing-smooth: cubic-bezier(0.25, 0.1, 0.25, 1);    
            --easing-spring: cubic-bezier(0.175, 0.885, 0.32, 1.275); 
      )HTML" \
R"HTML(      
            
            --duration-fast: 35ms;
            --duration-normal: 65ms;
            --duration-slow: 90ms;
            --duration-slower: 130ms;
        }
        
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;
            background:
                radial-gradient(circle at 14% 18%, rgba(0, 120, 212, 0.08), transparent 24%),
                radial-gradient(circle at 86% 12%, rgba(16, 110, 190, 0.06), transparent 22%),
                radial-gradient(circle at 82% 84%, rgba(0, 120, 212, 0.05), transparent 26%),
                linear-gradient(180deg, #fbfdff 0%, #f6f9fd 100%);
            height: 100vh;
            overflow: visible;
            color: var(--color-text-primary);
            font-size: 11px;
            font-weight: 400;
            position: relative;
        }

        body::before {
            content: '';
   )HTML" \
R"HTML(         position: fixed;
            inset: -30px;
            pointer-events: none;
            z-index: 0;
            background:
                radial-gradient(circle at 20% 20%, rgba(0, 120, 212, 0.10), transparent 26%),
                radial-gradient(circle at 80% 16%, rgba(0, 120, 212, 0.07), transparent 22%),
                radial-gradient(circle at 82% 78%, rgba(16, 110, 190, 0.06), transparent 24%),
                radial-gradient(circle at 14% 84%, rgba(0, 120, 212, 0.05), transparent 20%);
            filter: blur(18px);
            opacity: 0.72;
            animation: ambientDrift 18s ease-in-out infinite alternate;
        }
        
        
        .title-bar {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            height: 32px;
            background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
            display: flex;
            align-items: center;
            justify-content: space-between;
            )HTML" \
R"HTML(padding: 0 8px;
            z-index: 99999;
            cursor: default;
            user-select: none;
            border-bottom: 1px solid rgba(0, 0, 0, 0.08);
            box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
        }
        
        .title-bar-left {
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        .title-bar-icon {
            width: 18px;
            height: 18px;
            background: linear-gradient(135deg, #0078d4, #106ebe);
            border-radius: 4px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 10px;
            color: white;
            font-weight: 400;
            box-shadow: 0 1px 2px rgba(0, 120, 212, 0.3);
        }
        
        .title-bar-title {
            color: #1d1d1d;
            font-size: 12px;
            font-weight: 400;
            letter-spacing: 0.5px;
            white-space: nowrap;
        }
        
 )HTML" \
R"HTML(       .title-bar-controls {
            display: flex;
        }
        
        .title-bar-button {
            width: 46px;
            height: 32px;
            border: none;
            background: transparent;
            color: #494949;
            font-size: 12px;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: background 0.15s ease;
        }
        
        .title-bar-button:hover {
            background: rgba(0, 0, 0, 0.05);
        }
        
        .title-bar-button.close:hover {
            background: #e81123;
            color: white;
        }
        
        .title-bar-button svg {
            width: 12px;
            height: 12px;
            fill: currentColor;
        }
        
        
        
        .helper-info {
            
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            height: 20px;
         )HTML" \
R"HTML(   display: flex;
            align-items: center;
            padding: 4px 8px;
            background: var(--color-background-secondary);
            border-top: 1px solid var(--color-border);
            font-size: 9px;
            color: var(--color-text-secondary);
            width: 100%;
            
            z-index: 1;
            
            margin: 0;
            padding: 0 8px 0 8.5px; 
        }
        
        
        .helper-label {
            font-weight: 400;
            color: var(--color-primary);
            margin-right: 8px;
            white-space: nowrap;
        }
        
        
        .helper-text {
            flex: 1;
            transition: all 0.2s ease;
            min-width: 0;
        }
        
        
        .helper-text.slide-in {
            animation: slideIn 0.2s ease-out;
        }

        
        #speed-slider {
            -webkit-appearance: none;
            height: 4px;
            background: var(--color-background-secondary))HTML" \
R"HTML(;
            border-radius: 2px;
            outline: none;
            transition: background 0.2s ease;
        }

        #speed-slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: var(--color-primary);
            cursor: pointer;
            transition: all 0.2s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            box-shadow: 0 0 2px rgba(0, 0, 0, 0.2);
        }

        #speed-slider::-webkit-slider-thumb:hover {
            transform: scale(1.2);
            box-shadow: 0 0 0 4px rgba(0, 120, 212, 0.1);
        }

        #speed-slider::-webkit-slider-thumb:active {
            transform: scale(1.1);
            box-shadow: 0 0 0 6px rgba(0, 120, 212, 0.2);
        }

        @keyframes slideIn {
            from { 
                opacity: 0;
                transform: translateX(-10px); 
            }
            to { 
    )HTML" \
R"HTML(            opacity: 1;
                transform: translateX(0); 
            }
        }
        
        
        .container {
            width: 100%;
            
            height: 197px;
            
            top: 598px;
            display: flex;
            flex-direction: column;
            background: var(--color-background);
            box-shadow: var(--shadow-sm);
            position: absolute;
            left: 0;
            right: 0;
            padding-top: 0;
            
            z-index: 10;
        }
        
        
        .tabs {
            display: flex;
            background: var(--color-background);
            border-bottom: 1px solid var(--color-border);
            padding: 0;
            gap: 0;
            overflow-x: hidden;
            overflow-y: hidden;
            justify-content: flex-start;
        }
        
        
        .content {
            
            height: 152px;
            overflow-y: auto;
            padding: var(--sp)HTML" \
R"HTML(acing-md);
            background: var(--color-background);
        }
        .tabs::-webkit-scrollbar { height: 6px; }
        .tabs::-webkit-scrollbar-track { background: var(--color-background); }
        .tabs::-webkit-scrollbar-thumb { background: var(--color-border-hover); border-radius: var(--radius-full); transition: all var(--duration-normal) var(--easing-standard); }
        .tabs::-webkit-scrollbar-thumb:hover { background: var(--color-primary); box-shadow: var(--shadow-sm); }
        
        
        .tab-actions {
            display: flex;
            gap: 8px;
            margin-left: auto;
            align-items: center;
            flex-wrap: nowrap;
            flex: 0 0 auto;
            padding: 0 8px;
        }
        
        
        .tab-action-button {
            height: 24px;
            border: none;
            border-radius: var(--radius-sm);
            background: transparent;
            cursor: pointer;
            display: flex;
            align-i)HTML" \
R"HTML(tems: center;
            gap: 4px;
            padding: 0 8px;
            transition: background-color 0.1s linear;
            position: relative;
            flex: 0 0 auto;
            min-width: fit-content;
            white-space: nowrap;
            justify-content: center;
            color: var(--color-text-primary);
        }
        
        .tab-action-button:hover {
            background: var(--color-background-hover);
        }
        
        .tab-action-button:active {
            background: var(--color-background-secondary);
        }

        .tab-action-button:disabled {
            cursor: default;
            opacity: 0.7;
        }

        .tab-action-label {
            display: inline-flex;
            align-items: center;
            font-size: 9px;
            font-weight: 500;
            line-height: 1;
            color: inherit;
        }
        
        
        .icon-copy,
        .icon-key,
        .icon-refresh,
        .icon-refresh-nologin,
  )HTML" \
R"HTML(      .icon-mute,
        .icon-clear-cache {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            flex: 0 0 13px;
            width: 13px;
            height: 13px;
            position: relative;
            color: inherit;
        }

        .icon-copy svg,
        .icon-key svg,
        .icon-refresh svg,
        .icon-refresh-nologin svg,
        .icon-mute svg,
        .icon-clear-cache svg {
            display: block;
            width: 13px;
            height: 13px;
            overflow: visible;
        }

        .icon-copy svg *,
        .icon-key svg *,
        .icon-refresh svg *,
        .icon-refresh-nologin svg *,
        .icon-mute svg *,
        .icon-clear-cache svg * {
            fill: none;
            stroke: currentColor;
            stroke-width: 1.5;
            stroke-linecap: round;
            stroke-linejoin: round;
            vector-effect: non-scaling-stroke;
        }
        
        
  )HTML" \
R"HTML(      .tab-action-button:hover .icon-refresh svg {
            animation: rotate 1s linear infinite;
        }

        
        .icon-mute .mute-slash { display: none; }
        .tab-action-button.muted .icon-mute .mute-wave { display: none; }
        .tab-action-button.muted .icon-mute .mute-slash { display: inline; }
        
        
        .tab-action-button.muted,
        .tab-action-button.cache-clearing,
        .tab-action-button.cache-cleared {
            color: var(--color-primary);
        }

        .tab-action-button.cache-clear-failed {
            color: #c62828;
        }
        
        
        @keyframes rotate {
            from { transform: rotate(0deg); }
            to { transform: rotate(360deg); }
        }
        
        
        .tabs {
            overflow-x: hidden;
        }
        
        
        .tab-action-button {
            width: auto;
            min-width: 22px;
            height: 22px;
            gap: 2px;
            padding: 0 4px;
 )HTML" \
R"HTML(       }
        
        
        .icon-copy svg,
        .icon-key svg,
        .icon-refresh svg,
        .icon-refresh-nologin svg,
        .icon-mute svg,
        .icon-clear-cache svg {
            width: 13px;
            height: 13px;
        }
        
        
        .tab-actions {
            padding: 0 1px;
            gap: 0;
        }
        .tab-button { flex: 0 0 auto; padding: var(--spacing-sm) 12px; border: none; border-radius: 0; background: transparent; color: var(--color-text-secondary); font-size: 10.5px; font-weight: 400; cursor: pointer; transition: all var(--duration-normal) var(--easing-standard); position: relative; outline: none; opacity: 0; transform: translateY(-4px); animation: slideUp var(--duration-normal) var(--easing-enter) forwards; margin-bottom: -1px; min-width: 78px; text-align: center; } 
        .tab-button:nth-child(1) { animation-delay: 0.1s; }
        .tab-button:nth-child(2) { animation-delay: 0.15s; }
        .tab-button:nth-child(3) { an)HTML" \
R"HTML(imation-delay: 0.2s; }
        .tab-button:hover { color: var(--color-primary); background: var(--color-background-hover); }
        .tab-button.active { color: var(--color-primary); background: var(--color-background); }
        .tab-button.active::after { content: ''; position: absolute; bottom: 0; left: 0; right: 0; height: 2px; background: var(--color-primary); border-radius: var(--radius-full); transition: all var(--duration-fast) var(--easing-bounce); }
        .tab-button::before { content: ''; position: absolute; bottom: 0; left: 50%; right: 50%; height: 2px; background: var(--color-primary); border-radius: var(--radius-full); opacity: 0; transition: all var(--duration-normal) var(--easing-standard); }
        .tab-button:hover::before { left: 0; right: 0; opacity: 0.5; }
        
        .tab-button { display: inline-flex; align-items: center; justify-content: center; gap: 4px; }
        .tab-icon {
            display: inline-flex;
            align-items: center;
           )HTML" \
R"HTML( justify-content: center;
            flex: 0 0 13px;
            width: 13px;
            height: 13px;
            opacity: 0.9;
        }
        .tab-icon svg {
            display: block;
            width: 13px;
            height: 13px;
            overflow: visible;
        }
        .tab-icon svg * {
            fill: none;
            stroke: currentColor;
            stroke-width: 1.5;
            stroke-linecap: round;
            stroke-linejoin: round;
        }
        .tab-button.active .tab-icon { opacity: 1; }
        .content { 
            flex: 1; 
            padding: 0; 
            overflow: hidden; 
            background: var(--color-background); 
            display: flex;
            flex-direction: column;
        } 
        .tab-content { 
            display: none; 
            opacity: 0; 
            transform: translateY(3px); 
            transition: all var(--duration-normal) var(--easing-standard); 
            flex: 1;
            flex-direction: c)HTML" \
R"HTML(olumn;
            overflow-y: auto;
            padding: var(--spacing-lg); 
        } 
        #daily.tab-content {
            overflow-y: hidden;
            padding: 1px 4px 4px 4px;
        } 
        
        .tab-content:not(#daily) { padding: 12px 12px 12px 12px !important; }
        .tab-content:not(#daily) .section-title { font-size: 14px; margin-bottom: 12px; }
        .tab-content:not(#daily) .control-group { margin-bottom: 12px; }
        .tab-content:not(#daily) .label { font-size: 11px; margin-bottom: 5px; }
        .tab-content:not(#daily) .input { padding: 5px 8px; font-size: 11px; }
        .tab-content:not(#daily) .btn { padding: 5px 10px; font-size: 11px; }
        .tab-content:not(#daily) .list-item-title { font-size: 11px; }
        .tab-content:not(#daily) .list-item-subtitle { font-size: 9px; }
        .tab-content:not(#daily) .badge { font-size: 9px; }
        .tab-content:not(#daily) > div[style*="display: flex"] { margin-top: 0; }
        .tab-content.active)HTML" \
R"HTML( { display: flex; opacity: 1; transform: translateY(0); }
        .section-title { font-size: 14px; font-weight: 400; color: var(--color-text-primary); margin-bottom: var(--spacing-md); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; } 
        .control-group { margin-bottom: var(--spacing-md); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; } 
        .label { display: block; font-size: 11px; font-weight: 400; color: var(--color-text-primary); margin-bottom: var(--spacing-xs); } 
        .input { width: 100%; padding: var(--spacing-xs) var(--spacing-sm); border: 1px solid var(--color-border); border-radius: var(--radius-sm); font-size: 11px; background: var(--color-background); color: var(--color-text-primary); transition: all var(--duration-fast) var(--easing-standard); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var()HTML" \
R"HTML(--easing-enter) forwards; box-shadow: inset 0 1px 1px rgba(0, 0, 0, 0.05); } 
        .btn { padding: var(--spacing-xs) var(--spacing-md); border: 1px solid transparent; border-radius: var(--radius-sm); font-size: 11px; font-weight: 400; cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); margin-right: var(--spacing-xs); margin-bottom: var(--spacing-xs); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; box-shadow: none; } 
        .list-view { border: 1px solid var(--color-border); border-radius: var(--radius-sm); overflow: hidden; background: var(--color-background); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; box-shadow: none; } 
        .list-item { padding: var(--spacing-sm) var(--spacing-md); border-bottom: 1px solid var(--color-border); transition: all var(--duration-fast) var(--easing-standard); cursor: pointer; display: f)HTML" \
R"HTML(lex; justify-content: space-between; align-items: center; opacity: 0; transform: translateX(-6px); animation: fadeInLeft var(--duration-normal) var(--easing-enter) forwards; } 
        .list-item:last-child { border-bottom: none; }
        .list-item:hover { background: var(--color-background-hover); transform: translateX(0); box-shadow: none; } 
        .list-item-content { flex: 1; }
        .list-item-title { font-weight: 400; color: var(--color-text-primary); margin-bottom: var(--spacing-xs); font-size: 11px; } 
        .list-item-subtitle { font-size: 9px; color: var(--color-text-secondary); } 
        .badge { padding: var(--spacing-xs) var(--spacing-sm); border-radius: var(--radius-sm); font-size: 9px; font-weight: 400; } 
        .badge-success { background: #e8f5e8; color: #2e7d32; } 
        .badge-warning { background: #fff8e1; color: #f57c00; } 
        .badge-danger { background: #ffebee; color: #c62828; } 
        .form-row { display: flex; gap: var(--spacing-sm); margin-)HTML" \
R"HTML(bottom: var(--spacing-md); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; } 
        .form-col { flex: 1; }
        .divider { height: 1px; background: var(--color-border); margin: var(--spacing-md) 0; opacity: 0; animation: fadeIn var(--duration-normal) var(--easing-enter) forwards; } 
        
        
        @keyframes fadeIn { 
            from { opacity: 0; } 
            to { opacity: 1; } 
        }
        
        @keyframes fadeInUp { 
            from { opacity: 0; transform: translateY(6px); } 
            to { opacity: 1; transform: translateY(0); } 
        }
        
        @keyframes fadeInLeft { 
            from { opacity: 0; transform: translateX(-8px); } 
            to { opacity: 1; transform: translateX(0); } 
        }
        
        @keyframes slideUp { 
            from { opacity: 0; transform: translateY(-4px); } 
            to { opacity: 1; transform: translateY(0); } 
        }

        @)HTML" \
R"HTML(keyframes ambientDrift {
            0% { transform: translate3d(0, 0, 0) scale(1); }
            50% { transform: translate3d(-1.5%, -1.2%, 0) scale(1.025); }
            100% { transform: translate3d(1.2%, 1.5%, 0) scale(1.05); }
        }

        
        
        
        .panel-animate {
            opacity: 0;
            transform: translateY(4px);
            animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards;
        }
        
        .panel-animate:nth-child(1) { animation-delay: 0.02s; }
        .panel-animate:nth-child(2) { animation-delay: 0.04s; }
        .panel-animate:nth-child(3) { animation-delay: 0.06s; }

        

        
        
        
        .input {
            transition: all 0.2s linear; 
        }
        .input:focus { 
            outline: none; 
            border-color: var(--color-primary); 
            box-shadow: 0 0 0 2px rgba(0, 120, 212, 0.3); 
            transform: translateY(0); 
        }
        .input:hover { 
     )HTML" \
R"HTML(       border-color: var(--color-primary); 
            transform: translateY(0); 
        }
        .btn-primary {
            background: linear-gradient(180deg, var(--color-primary) 0%, var(--color-primary-hover) 100%);
            color: #ffffff;
            border-color: var(--color-primary);
            transition: transform var(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), background-color 0.1s linear, border-color 0.1s linear;
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.14);
        }
        .btn-primary:hover {
            background: linear-gradient(180deg, var(--color-primary-hover) 0%, #0d64b8 100%);
            border-color: var(--color-primary-hover);
            transform: translateY(-1px);
            box-shadow: 0 4px 12px rgba(0, 120, 212, 0.18), inset 0 1px 0 rgba(255, 255, 255, 0.12);
        }
        .btn-primary:active {
            transform: translateY(0);
            box-shadow: 0 2px 6px rgba)HTML" \
R"HTML((0, 120, 212, 0.12);
        }
        .btn-secondary {
            background: linear-gradient(180deg, var(--color-background) 0%, #fbfbfb 100%);
            color: var(--color-text-primary);
            border-color: var(--color-border);
            transition: transform var(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), background-color 0.1s linear, border-color 0.1s linear;
        }
        .btn-secondary:hover {
            background: linear-gradient(180deg, var(--color-background-secondary) 0%, #f6f6f6 100%);
            border-color: var(--color-primary);
            transform: translateY(-1px);
            box-shadow: 0 3px 10px rgba(0, 0, 0, 0.06);
        }
        .btn-secondary:active {
            transform: translateY(0);
            box-shadow: none;
        }
        
        
        ::-webkit-scrollbar { width: 6px; } 
        ::-webkit-scrollbar-track { background: var(--color-background); } 
        ::-webkit-scrol)HTML" \
R"HTML(lbar-thumb { background: var(--color-border); border-radius: var(--radius-full); transition: all var(--duration-fast) var(--easing-standard); } 
        ::-webkit-scrollbar-thumb:hover { background: var(--color-border-hover); } 
        
        
        #my-buf-list,
        #other-buf-list {
            overflow-x: hidden;
            overflow-y: hidden;
            overflow: hidden;
        }
        
        
        #my-buf-list.overflow-auto,
        #other-buf-list.overflow-auto {
            overflow-x: auto !important;
            overflow-y: hidden !important;
        }
        
        
        #my-buf-list::-webkit-scrollbar,
        #other-buf-list::-webkit-scrollbar {
            width: 6px;
            height: 6px;
            display: none;
        }
        
        
        #my-buf-list.overflow-auto::-webkit-scrollbar,
        #other-buf-list.overflow-auto::-webkit-scrollbar {
            display: block !important;
        }
        
        #my-buf-list::-webkit-scr)HTML" \
R"HTML(ollbar-track,
        #other-buf-list::-webkit-scrollbar-track {
            background: #e0e0e0;
            border-radius: 4px;
        }
        
        #my-buf-list::-webkit-scrollbar-thumb,
        #other-buf-list::-webkit-scrollbar-thumb {
            background: #888;
            border-radius: 4px;
        }
        
        #my-buf-list::-webkit-scrollbar-thumb:hover,
        #other-buf-list::-webkit-scrollbar-thumb:hover {
            background: #555;
        }
        
        
        .flat-pane {
            display: flex;
            align-items: stretch;
            height: 100%;
            min-height: 0;
        }

        .flat-sep {
            flex: none;
            width: 1px;
            margin: 1px 12px;
            background: linear-gradient(180deg, transparent, var(--color-border) 12%, var(--color-border) 88%, transparent);
        }

        .flat-head {
            display: flex;
            align-items: center;
            gap: 5px;
            min-heigh)HTML" \
R"HTML(t: 18px;
            margin-bottom: 5px;
        }

        .flat-dot {
            flex: none;
            width: 5px;
            height: 5px;
            border-radius: var(--radius-full);
            background: var(--color-primary);
        }

        .flat-dot.ally { background: #2e7d32; }
        .flat-dot.enemy { background: #d13438; }
        .flat-dot.goods { background: #f57c00; }
        .flat-dot.bag { background: var(--color-primary); }

        .flat-title {
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 0.02em;
            color: var(--color-text-primary);
            white-space: nowrap;
        }

        .flat-tools {
            display: flex;
            align-items: center;
            gap: 4px;
            margin-left: auto;
        }

        .flat-link {
            padding: 1px 3px;
            font-size: 9px;
            color: var(--color-text-tertiary);
            background: transparent;
            border: none;
  )HTML" \
R"HTML(          border-radius: var(--radius-sm);
            cursor: pointer;
            transition: color var(--duration-fast) var(--easing-standard), background var(--duration-fast) var(--easing-standard);
        }

        .flat-link:hover {
            color: var(--color-primary);
            background: var(--color-background-hover);
        }

        .flat-empty {
            padding: 14px 0;
            font-size: 9px;
            color: var(--color-text-tertiary);
            text-align: center;
        }

        
        .bp-side {
            flex: 1 1 300px;
            display: flex;
            flex-direction: column;
            min-width: 220px;
            min-height: 0;
            padding-left: 8px;
            border-left: 2px solid transparent;
        }

        .bp-ally { border-left-color: rgba(46, 125, 50, 0.38); }
        .bp-enemy { border-left-color: rgba(209, 52, 56, 0.38); }
        .bp-goods { border-left-color: rgba(245, 124, 0, 0.38); }

        .bp-side >)HTML" \
R"HTML( .flat-head,
        .bp-items > .flat-head {
            padding-bottom: 3px;
            border-bottom: 1px solid rgba(0, 0, 0, 0.07);
        }

        .bp-select {
            flex: 0 1 168px;
            min-width: 88px;
            margin-left: auto;
            height: 19px;
            padding: 0 3px;
            font-size: 9.5px;
            color: var(--color-text-primary);
            background: var(--color-background);
            border: 1px solid var(--color-border);
            border-radius: var(--radius-sm);
            outline: none;
        }

        .bp-select:hover { border-color: var(--color-border-hover); }
        .bp-select:focus { border-color: var(--color-primary); }

        
        .bp-idline {
            display: flex;
            align-items: baseline;
            gap: 6px;
            min-height: 16px;
            margin-bottom: 3px;
        }

        .bp-name {
            font-size: 11px;
            font-weight: 600;
            line-height: 16p)HTML" \
R"HTML(x;
            color: var(--color-text-primary);
            overflow: hidden;
            white-space: nowrap;
            text-overflow: ellipsis;
        }

        .bp-sid {
            flex: none;
            font-size: 8px;
            line-height: 12px;
            color: var(--color-text-tertiary);
            font-variant-numeric: tabular-nums;
        }

        .bp-sid b {
            font-weight: 500;
            color: var(--color-text-secondary);
        }

        .bp-hp {
            flex: none;
            margin-left: auto;
            font-size: 11px;
            font-weight: 600;
            line-height: 16px;
            color: #d13438;
            font-variant-numeric: tabular-nums;
        }

        
        .bp-bufs {
            display: flex;
            flex-wrap: nowrap;
            align-items: center;
            gap: 3px;
            height: 0;
            overflow: hidden;
        }

        .bp-bufs.has-bufs {
            height: 17px;
            marg)HTML" \
R"HTML(in-bottom: 2px;
            overflow-x: auto;
            overflow-y: hidden;
        }

        .bp-bufs::-webkit-scrollbar { height: 3px; }
        .bp-bufs::-webkit-scrollbar-track { background: transparent; }
        .bp-bufs::-webkit-scrollbar-thumb {
            background: rgba(0, 0, 0, 0.16);
            border-radius: 2px;
        }

        .buf-round {
            font-style: normal;
            margin-left: 3px;
            font-size: 7px;
            opacity: 0.75;
        }

        .bp-skills {
            display: flex;
            flex-direction: column;
            flex: 1;
            min-height: 0;
            margin-top: 2px;
        }

        .bp-skill-head {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 0 2px 2px;
            font-size: 8px;
            line-height: 11px;
            letter-spacing: 0.05em;
            color: var(--color-text-tertiary);
            border-bottom: 1px )HTML" \
R"HTML(solid var(--color-border);
        }

        .bp-skill-body {
            flex: 1;
            min-height: 0;
            overflow-y: auto;
        }

        .skill-row {
            display: grid;
            grid-template-columns: minmax(0, 1fr) auto;
            align-items: center;
            gap: 8px;
            min-height: 15px;
            padding: 0 2px;
            border-bottom: 1px solid rgba(0, 0, 0, 0.04);
            overflow: hidden;
        }

        .skill-row:nth-child(even) { background: rgba(0, 0, 0, 0.015); }

        .skill-row:hover { background: var(--color-background-hover); }

        .skill-row > div {
            overflow: hidden;
            font-size: 10px;
            line-height: 15px;
            white-space: nowrap;
            text-overflow: ellipsis;
        }

        .skill-row > div:last-child {
            color: var(--color-text-secondary);
            font-variant-numeric: tabular-nums;
        }

        
        .bp-items {
            f)HTML" \
R"HTML(lex: 1 2 260px;
            display: flex;
            flex-direction: column;
            min-width: 210px;
            min-height: 0;
        }

        .bp-items-grid {
            display: grid;
            grid-template-columns: repeat(4, minmax(0, 1fr));
            align-content: start;
            gap: 3px;
            flex: 1;
            min-height: 0;
            overflow-y: auto;
        }

        .bp-hint {
            margin-top: 4px;
            font-size: 8.5px;
            color: var(--color-text-tertiary);
            overflow: hidden;
            white-space: nowrap;
            text-overflow: ellipsis;
        }

        .bp-actions {
            display: flex;
            align-items: center;
            gap: 4px;
            margin-top: 5px;
        }

        .bp-num {
            flex: none;
            width: 34px;
            height: 20px;
            padding: 0 3px;
            font-size: 9.5px;
            color: var(--color-text-primary);
            text-)HTML" \
R"HTML(align: center;
            background: var(--color-background);
            border: 1px solid var(--color-border);
            border-radius: var(--radius-sm);
            outline: none;
        }

        .bp-num:focus { border-color: var(--color-primary); }

        .bp-btn {
            flex: 1;
            height: 20px;
            padding: 0 6px;
            font-size: 9.5px;
            color: #ffffff;
            background: var(--color-primary);
            border: none;
            border-radius: var(--radius-sm);
            cursor: pointer;
            transition: background var(--duration-fast) var(--easing-standard);
        }

        .bp-btn:hover { background: var(--color-primary-hover); }

        .bp-btn.ghost {
            color: var(--color-text-primary);
            background: var(--color-background-secondary);
        }

        .bp-btn.ghost:hover { background: var(--color-background-hover); }

        
        .mb-list {
            display: flex;
            f)HTML" \
R"HTML(lex-direction: column;
            flex: 1;
            min-width: 0;
            min-height: 0;
        }

        .mb-table {
            display: flex;
            flex-direction: column;
            flex: 1;
            min-height: 0;
        }

        .mb-thead {
            display: grid;
            grid-template-columns: 24px 48px 26px 24px 24px 26px 22px 26px 150px 1fr;
            padding: 0 4px 2px;
            font-size: 8.5px;
            color: var(--color-text-tertiary);
            border-bottom: 1px solid var(--color-border);
        }

        .mb-row {
            display: grid;
            grid-template-columns: 24px 48px 26px 24px 24px 26px 22px 26px 150px 1fr;
            align-items: center;
            min-height: 22px;
            padding: 0 4px;
            font-size: 9px;
            color: var(--color-text-primary);
            border-bottom: 1px solid rgba(0, 0, 0, 0.04);
            cursor: pointer;
        }

        .mb-row:hover { background: var(--col)HTML" \
R"HTML(or-background-hover); }

        .mb-row.is-selected {
            background: rgba(0, 120, 212, 0.08);
            box-shadow: inset 2px 0 0 var(--color-primary);
        }

        .mb-row > span {
            overflow: hidden;
            white-space: nowrap;
            text-overflow: ellipsis;
        }

        .mb-detail {
            display: flex;
            flex-direction: column;
            flex: none;
            width: 268px;
            min-height: 0;
        }

        .mb-detail-body {
            flex: 1;
            min-height: 0;
            overflow-y: auto;
            font-size: 9px;
        }

        .mb-sec {
            display: flex;
            align-items: baseline;
            gap: 4px;
            margin: 5px 0 2px;
            padding-bottom: 1px;
            font-size: 8px;
            letter-spacing: 0.03em;
            color: var(--color-text-tertiary);
            border-bottom: 1px solid rgba(0, 0, 0, 0.05);
        }

        .mb-sec:first-child )HTML" \
R"HTML({ margin-top: 0; }

        .mb-kv {
            display: grid;
            grid-template-columns: repeat(2, minmax(0, 1fr));
            gap: 1px 10px;
        }

        .mb-kv.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }

        .mb-kv > div {
            display: flex;
            align-items: baseline;
            justify-content: space-between;
            gap: 3px;
            min-width: 0;
            line-height: 13px;
        }

        .mb-kv .bp-k {
            display: inline;
            font-size: 8px;
            letter-spacing: 0.02em;
            color: var(--color-text-tertiary);
            line-height: 13px;
            white-space: nowrap;
        }

        .mb-kv .bp-v {
            display: inline;
            font-size: 9px;
            font-weight: 600;
            color: var(--color-text-primary);
            line-height: 13px;
            font-variant-numeric: tabular-nums;
        }

        .mb-chip {
            display: inline-block;
   )HTML" \
R"HTML(         padding: 1px 5px;
            margin: 0 3px 3px 0;
            font-size: 8.5px;
            color: #2e7d32;
            background: #e8f5e8;
            border-radius: var(--radius-full);
        }

        
        .sub-tabs {
            display: flex;
            background: var(--color-background-secondary);
            border-bottom: 1px solid var(--color-border);
            padding: 2px;
            gap: 4px;
            margin-bottom: 6px;
            border-radius: var(--radius-sm);
            justify-content: flex-start;
        }
        
        .sub-tab-button {
            width: 75px;
            height: 22px;
            display: flex;
            align-items: center;
            justify-content: center;
            border: none;
            background: transparent;
            color: var(--color-text-secondary);
            font-size: 10px;
            font-weight: 400;
            cursor: pointer;
            transition: all var(--duration-fast) var(--easin)HTML" \
R"HTML(g-standard);
            border-radius: var(--radius-sm);
        }
        
        .sub-tab-button:hover {
            background: var(--color-background-hover);
            color: var(--color-primary);
        }
        
        .sub-tab-button.active {
            background: linear-gradient(180deg, var(--color-primary) 0%, var(--color-primary-hover) 100%);
            color: white;
            box-shadow: 0 3px 8px rgba(0, 120, 212, 0.16);
        }
        
        .sub-tab-content {
            display: none;
            flex: 1;
            overflow-y: auto;
            overflow-x: visible;
            padding-right: 2px; 
        }
        
        .sub-tab-content.active {
            display: block;
            animation: fadeIn var(--duration-fast) var(--easing-enter);
        }
        
        
        .tab-content.active .sub-tab-content.active {
            display: flex;
            flex-direction: column;
        }

        
        .fx-page {
            display: fle)HTML" \
R"HTML(x;
            flex-direction: column;
            gap: 4px;
            flex: 1;
            min-height: 0;
            padding: 3px 6px;
        }
        
        .fx-row {
            display: flex;
            gap: 8px;
            align-items: stretch;
            flex: 1;
            min-height: 0;
        }
        
        .fx-panel {
            display: flex;
            flex-direction: column;
            gap: 3px;
            min-height: 0;
            min-width: 0;
            background: var(--color-background);
            border: 1px solid var(--color-border);
            border-radius: 4px;
            padding: 5px 6px;
            box-shadow: var(--shadow-sm);
            overflow: hidden;
        }
        .fx-panel-grow { flex: 1 1 auto; }
        .fx-panel-title {
            font-size: 11px;
            font-weight: 600;
            color: var(--color-text-primary);
            line-height: 1.2;
            flex: 0 0 auto;
        }
        .fx-panel-note {
     )HTML" \
R"HTML(       font-size: 8.5px;
            color: var(--color-text-tertiary);
            line-height: 1.35;
            flex: 0 0 auto;
        }
        
        .fx-scroll {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            overflow-x: hidden;
        }
        
        .fx-actions {
            display: flex;
            gap: 4px;
            flex: 0 0 auto;
            margin-top: auto;
        }
        .fx-actions > .btn { margin: 0; }
        
        .fx-checks {
            display: grid;
            gap: 2px 4px;
            align-content: start;
        }
        .fx-check {
            display: flex;
            align-items: center;
            gap: 3px;
            padding: 1px 4px;
            background: #FFFFFF;
            border-radius: 2px;
            cursor: pointer;
            font-size: 9.5px;
            color: var(--color-text-primary);
            white-space: nowrap;
        }
        
        .fx-sep {
            w)HTML" \
R"HTML(idth: 1px;
            background: var(--color-border);
            flex: 0 0 auto;
        }
        
        .hw-groups {
            display: flex;
            flex-wrap: wrap;
            gap: 3px;
            flex: 0 0 auto;
        }
        .hw-group {
            display: flex;
            align-items: center;
            gap: 3px;
            font-size: 9.5px;
            line-height: 1.5;
            padding: 0 7px;
            border: 1px solid var(--color-border);
            border-radius: 9px;
            background: #FFFFFF;
            color: var(--color-text-secondary);
            cursor: pointer;
            white-space: nowrap;
        }
        .hw-group:hover { border-color: var(--color-border-hover); background: var(--color-background-hover); }
        .hw-group.active {
            background: var(--color-primary);
            border-color: var(--color-primary);
            color: #FFFFFF;
            font-weight: 600;
        }
        .hw-group-count { font-si)HTML" \
R"HTML(ze: 8.5px; opacity: 0.85; }
        .hw-tasks {
            display: flex;
            flex-wrap: wrap;
            gap: 3px;
            align-content: flex-start;
            flex: 1 1 auto;
            min-height: 0;
            overflow: hidden;
        }
        .hw-task {
            display: flex;
            align-items: center;
            gap: 3px;
            font-size: 9.5px;
            line-height: 1.5;
            padding: 0 6px;
            border: 1px solid #D9E4F1;
            border-radius: 4px;
            background: #FFFFFF;
            color: var(--color-text-primary);
            cursor: pointer;
            white-space: nowrap;
        }
        .hw-task:hover { border-color: var(--color-border-hover); background: var(--color-background-hover); }
        .hw-task input[type="checkbox"] { width: 11px; height: 11px; margin: 0; cursor: pointer; }
        .hw-task.hw-done { opacity: 0.5; }
        .hw-task.hw-accepted { border-color: var(--color-primary); }
       )HTML" \
R"HTML( .hw-foot {
            display: flex;
            align-items: center;
            gap: 6px;
            flex: 0 0 auto;
            min-width: 0;
            font-size: 9px;
            color: var(--color-text-secondary);
        }
        .hw-foot > span {
            background: var(--color-background-secondary);
            border-radius: 2px;
            padding: 2px 6px;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        #huangcheng-weiji-summary { flex: 0 0 auto; }
        #huangcheng-weiji-status { flex: 1 1 auto; min-width: 0; }
        
        .act-item {
            display: flex;
            align-items: center;
            gap: 5px;
            min-width: 0;
            padding: 1px 6px;
            background: #FFFFFF;
            border: 1px solid #D9E4F1;
            border-radius: 4px;
            cursor: pointer;
            font-size: 10px;
            color: var(--color-text-primary);
            t)HTML" \
R"HTML(ransition: border-color var(--duration-fast) var(--easing-standard), background var(--duration-fast) var(--easing-standard);
        }
        .act-item:hover { border-color: var(--color-border-hover); background: var(--color-background-hover); }
        
        .act-item.act-disabled {
            opacity: 0.55;
            cursor: not-allowed;
            background: var(--color-background-secondary);
        }
        .act-item.act-disabled .act-item-name { color: var(--color-text-tertiary); }
        .act-item > input[type="checkbox"].act-pick {
            flex: 0 0 auto;
            width: 12px;
            height: 12px;
            cursor: pointer;
        }
        .act-item-name {
            flex: 1 1 auto;
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .act-item-count {
            flex: 0 0 auto;
            min-width: 30px;
            text-align: center;
            font-size)HTML" \
R"HTML(: 9px;
            font-weight: 600;
            color: var(--color-primary);
            background: #e8f4fd;
            border-radius: 3px;
            padding: 0 4px;
            line-height: 1.5;
        }
        .act-item-count.act-count-zero {
            color: var(--color-text-tertiary);
            background: var(--color-background-secondary);
        }
        .act-item-count.act-count-unknown {
            color: var(--color-text-tertiary);
            background: transparent;
        }

        .btn,
        .sub-tab-button,
        .tab-action-button {
            position: relative;
            overflow: hidden;
            will-change: transform, box-shadow, background-color, border-color;
            transform: translateZ(0);
        }

        .tab-button {
            will-change: transform, box-shadow, background-color, border-color;
        }

        .btn:hover,
        .sub-tab-button:hover,
        .tab-action-button:hover,
        .tab-button:hover {
        )HTML" \
R"HTML(    transform: translateY(-1px);
        }

        .btn:active,
        .sub-tab-button:active,
        .tab-action-button:active,
        .tab-button:active {
            transform: translateY(0);
        }

        
        #sub-spiritcollect {
            padding: 4px 6px;
            box-sizing: border-box;
            height: 100%;
            min-height: 0;
            overflow: hidden;
        }

        .tab-content.active #sub-spiritcollect.active {
            display: flex;
            flex-direction: column;
        }

        #sub-spiritcollect .spirit-collect-layout {
            flex: 1;
            min-height: 0;
            display: flex;
            flex-direction: column;
            gap: 5px;
            overflow: hidden;
        }

        
        .spirit-toolbar {
            flex: 0 0 auto;
            display: flex;
            align-items: center;
            gap: 6px;
            min-width: 0;
            height: 26px;
            overflow: hidden;
        })HTML" \
R"HTML(

        .spirit-toolbar .spirit-stat-chip {
            flex: 0 0 auto;
        }

        .spirit-toolbar-selected {
            flex: 1 1 auto;
            min-width: 0;
            font-size: 9.5px;
            color: var(--color-text-secondary);
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-toolbar-send {
            flex: 0 0 auto;
            display: flex;
            align-items: center;
            gap: 4px;
        }

        .spirit-toolbar-send label {
            font-size: 9.5px;
            color: var(--color-text-secondary);
            white-space: nowrap;
        }

        .spirit-toolbar-send input {
            width: 96px;
            height: 22px;
            border-radius: 5px;
            border: 1px solid var(--color-border);
            font-size: 9.5px;
            padding: 2px 6px;
            box-sizing: border-box;
        }

        .spirit-toolbar-send .btn {
            )HTML" \
R"HTML(font-size: 9.5px;
            padding: 3px 10px;
            min-height: 22px;
            margin: 0;
            line-height: 1.1;
        }

        .spirit-toolbar .spirit-status-line {
            flex: 0 1 auto;
            max-width: 200px;
        }

        .spirit-toolbar > .btn {
            flex: 0 0 auto;
            font-size: 9.5px;
            padding: 3px 10px;
            min-height: 22px;
            margin: 0;
            line-height: 1.1;
        }

        
        #sub-spiritcollect .spirit-columns {
            flex: 1 1 auto;
            min-height: 0;
            display: flex;
            gap: 8px;
            align-items: stretch;
            overflow: hidden;
        }

        .spirit-giving-panel,
        .spirit-record-side {
            display: flex;
            flex-direction: column;
            gap: 4px;
            min-width: 0;
            min-height: 0;
            background: var(--color-background);
            border: 1px solid var(--color-bord)HTML" \
R"HTML(er);
            border-radius: 6px;
            padding: 6px 7px;
            box-shadow: var(--shadow-sm);
            overflow: hidden;
        }

        .spirit-giving-panel {
            flex: 1.25;
        }

        .spirit-giving-actions {
            display: flex;
            flex-wrap: nowrap;
            gap: 4px;
            flex: 0 0 auto;
        }

        .spirit-giving-actions .btn {
            font-size: 8.5px;
            padding: 2px 6px;
            min-height: 20px;
            margin: 0;
            line-height: 1.1;
        }

        .spirit-stat-chip {
            display: inline-flex;
            align-items: center;
            gap: 4px;
            padding: 2px 6px;
            background: #f5f8fc;
            border: 1px solid #dbe7f2;
            border-radius: 6px;
            white-space: nowrap;
        }

        
        #spirit-list {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            border: 1px soli)HTML" \
R"HTML(d var(--color-border);
            border-radius: 6px;
            padding: 3px;
            background: linear-gradient(180deg, #ffffff 0%, #f7fbff 100%);
            font-size: 10px;
            scrollbar-gutter: stable;
        }

        #sub-spiritcollect.sub-tab-content {
            overflow: hidden;
            padding-right: 0;
        }

        .spirit-list-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(190px, 1fr));
            gap: 3px;
            align-content: start;
        }

        .spirit-list-empty {
            color: var(--color-text-tertiary);
            text-align: center;
            padding: 10px 8px;
        }

        
        .spirit-item {
            position: relative;
            padding: 3px 6px;
            cursor: pointer;
            border-radius: 6px;
            border: 1px solid #d9e4ef;
            background: linear-gradient(180deg, #fbfdff 0%, #f1f6fb 100%);
            transition: transform v)HTML" \
R"HTML(ar(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), border-color var(--duration-fast) var(--easing-standard), background var(--duration-fast) var(--easing-standard);
            min-width: 0;
            overflow: hidden;
        }

        .spirit-item:hover {
            transform: translateY(-1px);
            border-color: #b8d3ea;
            box-shadow: 0 4px 10px rgba(0, 120, 212, 0.12);
        }

        .spirit-item.selected {
            border-color: #6fb6f0;
            background: linear-gradient(135deg, #eaf6ff 0%, #dcefff 100%);
            box-shadow: 0 6px 14px rgba(0, 120, 212, 0.18);
        }

        .spirit-item.selected::after {
            content: '';
            position: absolute;
            inset: 0;
            border-radius: inherit;
            box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.7);
            pointer-events: none;
        }

        
        .spirit-item-top {
            display: flex;
  )HTML" \
R"HTML(          align-items: center;
            gap: 6px;
            min-width: 0;
        }

        .spirit-item-name {
            flex: 0 1 auto;
            min-width: 0;
            font-size: 9.5px;
            font-weight: 700;
            color: var(--color-text-primary);
            line-height: 1.2;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-item-tag {
            flex: 0 0 auto;
            padding: 0 5px;
            border-radius: 999px;
            background: rgba(0, 120, 212, 0.1);
            color: var(--color-primary);
            font-weight: 700;
            font-size: 8.5px;
            line-height: 1.3;
            white-space: nowrap;
        }

        .spirit-item-chips {
            display: flex;
            flex-wrap: nowrap;
            gap: 3px;
            min-width: 0;
            overflow: hidden;
        }

        .spirit-mini-chip {
            display: inline-flex;
   )HTML" \
R"HTML(         align-items: center;
            min-width: 0;
            max-width: 100%;
            padding: 0 4px;
            border-radius: 999px;
            background: rgba(0, 0, 0, 0.04);
            color: var(--color-text-secondary);
            font-size: 8.5px;
            line-height: 1.3;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-item-detail {
            display: flex;
            flex-direction: column;
            gap: 1px;
            font-size: 8.5px;
            color: var(--color-text-secondary);
            line-height: 1.3;
        }

        .spirit-item-detail-line {
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-status-line {
            font-size: 9.5px;
            color: var(--color-text-tertiary);
            padding: 0 2px;
            line-height: 1.25;
            flex:)HTML" \
R"HTML( 0 1 auto;
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-record-side {
            flex: 1;
            min-width: 0;
            min-height: 0;
            gap: 3px;
        }

        .spirit-record-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 6px;
            flex: 0 0 auto;
        }

        .spirit-record-title {
            font-size: 10.5px;
            font-weight: 700;
            color: var(--color-text-primary);
        }

        
        .spirit-record-meta {
            display: flex;
            align-items: center;
            gap: 6px;
            flex: 0 0 auto;
            min-width: 0;
        }

        .spirit-record-subtitle {
            font-size: 9.5px;
            color: var(--color-text-tertiary);
            flex: 1 1 auto;
            min-width: 0;
            white-)HTML" \
R"HTML(space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-record-body {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            background: linear-gradient(180deg, #fffdf8 0%, #f8fbff 100%);
            border: 1px solid var(--color-border);
            border-radius: 6px;
            padding: 4px 6px;
            font-size: 10px;
            line-height: 1.35;
            color: var(--color-text-secondary);
            white-space: pre-wrap;
            word-break: break-word;
        }

        @media (max-width: 960px) {
            .spirit-list-grid {
                grid-template-columns: repeat(2, minmax(0, 1fr));
            }

            .spirit-toolbar {
                height: auto;
                flex-wrap: wrap;
            }

            .spirit-toolbar-send input {
                width: 80px;
            }
        }

        @media (max-width: 760px) {
            .spirit-list-g)HTML" \
R"HTML(rid {
                grid-template-columns: minmax(0, 1fr);
            }

            #sub-spiritcollect .spirit-columns {
                flex-direction: column;
            }

            .spirit-toolbar-send input {
                width: 100%;
            }
        }

        
        .item-btn {
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 3px 2px;
            font-size: 9px;
            color: var(--color-text-primary);
            background: #f8f9fb;
            border: 1px solid rgba(0, 0, 0, 0.06);
            border-radius: 4px;
            cursor: pointer;
            white-space: nowrap;
            transition: background var(--duration-fast) var(--easing-standard), border-color var(--duration-fast) var(--easing-standard);
        }

        .item-btn:hover {
            background: #eef3fa;
            border-color: rgba(0, 120, 212, 0.45);
        }

        .item-btn.selected {
            color: )HTML" \
R"HTML(var(--color-primary);
            font-weight: 600;
            background: rgba(0, 120, 212, 0.08);
            border-color: rgba(0, 120, 212, 0.35);
        }

        
        .packet-list {
            overflow-y: auto;
            height: calc(100% - 64px);
            padding: 4px;
            border-bottom: 1px solid var(--color-border);
        }
        
        .packet-item {
            padding: 2.8px;
            border-bottom: 1px solid var(--color-border);
            font-size: 7px;
            cursor: pointer;
            transition: none;
            border-radius: 1.4px;
        }
        
        .packet-item:hover {
            background: var(--color-background-hover);
        }
        
        .packet-item.selected {
            background: rgba(0, 120, 212, 0.1);
            border-left: 2.1px solid var(--color-primary);
        }
        
        .packet-send-container {
            padding: 2.8px;
            display: flex;
            gap: 2.8px;
           )HTML" \
R"HTML( align-items: center;
            background: var(--color-background-secondary);
        }
        
        .packet-hex-input {
            flex: 1;
            font-size: 9px;
            padding: 3px 6px;
            border: 1px solid var(--color-border);
            border-radius: var(--radius-sm);
            transition: all 0.2s linear;
        }
        
        .packet-hex-input:focus {
            outline: none;
            border-color: var(--color-primary);
            box-shadow: 0 0 0 2px rgba(0, 120, 212, 0.3);
        }
        
        .send-packet-btn {
            padding: 3px 8px;
            font-size: 9px;
            background: var(--color-primary);
            color: white;
            border: none;
            border-radius: var(--radius-sm);
            cursor: pointer;
            transition: all 0.2s linear;
        }
        
        .send-packet-btn:hover {
            background: var(--color-primary-hover);
        }

        
        .update-dialog-overla)HTML" \
R"HTML(y {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0, 0, 0, 0.3);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 999999;
            opacity: 0;
            visibility: hidden;
            transition: opacity 0.2s ease;
        }

        .update-dialog-overlay.show {
            opacity: 1;
            visibility: visible;
        }

        .update-dialog {
            background: #ffffff;
            border-radius: 8px;
            box-shadow: 0 4px 20px rgba(0, 0, 0, 0.15);
            width: 380px;
            max-width: 90%;
            overflow: hidden;
        }

        .update-dialog-header {
            background: #f8fafc;
            color: #334155;
            padding: 16px 18px;
            font-size: 13px;
            font-weight: 600;
            display: flex;
            align-items: center;
          )HTML" \
R"HTML(  gap: 10px;
            border-bottom: 1px solid #e2e8f0;
        }

        .update-dialog-header-icon {
            width: 28px;
            height: 28px;
            background: #3b82f6;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            flex-shrink: 0;
        }

        .update-dialog-header-icon svg {
            width: 16px;
            height: 16px;
            fill: none;
            stroke: white;
            stroke-width: 2;
        }

        .update-dialog-body {
            padding: 18px;
            max-height: 300px;
            overflow-y: auto;
            background: #ffffff;
        }

        .update-dialog-announcement {
            font-size: 12px;
            font-weight: 600;
            color: #1e293b;
            margin-bottom: 12px;
            padding: 10px 12px;
            background: #eff6ff;
            border-radius: 6px;
            border-left: 3px solid #3b82f)HTML" \
R"HTML(6;
        }

        .update-dialog-content {
            font-size: 11px;
            color: #64748b;
            line-height: 1.6;
            white-space: pre-wrap;
        }

        .update-dialog-footer {
            padding: 14px 18px;
            background: #f8fafc;
            display: flex;
            justify-content: flex-end;
            gap: 8px;
            border-top: 1px solid #e2e8f0;
        }

        .update-dialog-btn {
            padding: 8px 18px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: 500;
            cursor: pointer;
            transition: background 0.15s ease;
        }

        .update-dialog-btn.primary {
            background: #3b82f6;
            color: white;
            border: none;
        }

        .update-dialog-btn.primary:hover {
            background: #2563eb;
        }

        .update-dialog-btn.secondary {
            background: transparent;
            color: #64748b;
            border:)HTML" \
R"HTML( 1px solid #e2e8f0;
        }

        .update-dialog-btn.secondary:hover {
            background: #f1f5f9;
        }
        .tab-button,
        .sub-tab-button,
        .tab-action-button,
        .btn {
            position: relative;
            overflow: hidden;
            will-change: transform, box-shadow, background-color, border-color;
        }

        .tab-button:hover,
        .sub-tab-button:hover,
        .tab-action-button:hover,
        .btn:hover {
            transform: translateY(-1px);
        }

        .tab-button:active,
        .sub-tab-button:active,
        .tab-action-button:active,
        .btn:active {
            transform: translateY(0);
        }

        .tab-button.active {
            box-shadow: inset 0 -2px 0 rgba(0, 120, 212, 0.18), 0 2px 8px rgba(0, 120, 212, 0.08);
        }

        .sub-tab-button.active {
            box-shadow: 0 3px 8px rgba(0, 120, 212, 0.16);
        }

        
        #helper-log.tab-content.active {
            padd)HTML" \
R"HTML(ing: 3px 6px 4px 6px !important;
            overflow: hidden;
        }

        .log-panel {
            display: flex;
            flex-direction: column;
            gap: 3px;
            flex: 1 1 auto;
            min-height: 0;
        }

        
        .log-toolbar {
            flex: 0 0 auto;
            height: 16px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 8px;
        }
        .log-toolbar-left {
            display: flex;
            align-items: center;
            gap: 5px;
            min-width: 0;
        }
        .log-status-dot {
            flex: 0 0 auto;
            width: 5px;
            height: 5px;
            border-radius: 50%;
            background: var(--color-primary);
            box-shadow: 0 0 0 2px rgba(0, 120, 212, 0.13);
        }
        .log-count {
            font-size: 10px;
            line-height: 1;
            color: var(--color-text-secondary);
          )HTML" \
R"HTML(  font-variant-numeric: tabular-nums;
            white-space: nowrap;
        }
        .log-count b {
            font-weight: 600;
            color: var(--color-primary);
            margin: 0 1px;
        }
        .log-clear-btn {
            display: inline-flex;
            align-items: center;
            gap: 3px;
            height: 16px;
            padding: 0 7px;
            border: 1px solid var(--color-border);
            border-radius: var(--radius-sm);
            background: var(--color-background);
            color: var(--color-text-secondary);
            font-size: 9.5px;
            line-height: 1;
            cursor: pointer;
            transition: background var(--duration-fast) var(--easing-standard),
                        color var(--duration-fast) var(--easing-standard),
                        border-color var(--duration-fast) var(--easing-standard);
        }
        .log-clear-btn svg {
            width: 10px;
            height: 10px;
            f)HTML" \
R"HTML(ill: none;
            stroke: currentColor;
            stroke-width: 1.4;
            stroke-linecap: round;
            stroke-linejoin: round;
        }
        .log-clear-btn:hover {
            color: #c62828;
            border-color: rgba(198, 40, 40, 0.4);
            background: #fff6f6;
        }
        .log-clear-btn:active {
            background: #ffecec;
        }

        
        .log-table {
            --log-cols: 34px 60px minmax(0, 1fr);
            flex: 1 1 auto;
            min-height: 0;
            display: flex;
            flex-direction: column;
            border: 1px solid var(--color-border);
            border-radius: 4px;
            background: var(--color-background);
            overflow: hidden;
        }
        
        .log-body {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            overflow-x: hidden;
            overscroll-behavior: contain;
        }
        .log-thead,
        .log-row {
        )HTML" \
R"HTML(    display: grid;
            grid-template-columns: var(--log-cols);
            column-gap: 8px;
            align-items: center;
            padding: 0 8px;
            box-sizing: border-box;
            flex: 0 0 auto;
        }
        .log-thead {
            position: sticky;
            top: 0;
            z-index: 2;
            height: 17px;
            font-size: 9px;
            font-weight: 600;
            letter-spacing: 0.4px;
            color: #5b6b7c;
            background: linear-gradient(180deg, #f8fbff 0%, #eef4fb 100%);
            border-bottom: 1px solid var(--color-border);
            user-select: none;
        }
        .log-thead .log-cell {
            color: #5b6b7c;
            font-family: inherit;
            font-size: 9px;
            font-weight: 600;
        }
        .log-body::-webkit-scrollbar {
            width: 5px;
        }
        .log-body::-webkit-scrollbar-track {
            background: transparent;
        }
        .log-body::-web)HTML" \
R"HTML(kit-scrollbar-thumb {
            background: #d7dde5;
            border-radius: var(--radius-full);
        }
        .log-body::-webkit-scrollbar-thumb:hover {
            background: var(--color-primary);
        }

        
        .log-row {
            min-height: 17px;
            font-size: 10px;
            line-height: 1.35;
            color: var(--color-text-primary);
            border-bottom: 1px solid #f1f4f8;
            position: relative;
            transition: background var(--duration-fast) var(--easing-standard);
        }
        .log-row:last-child {
            border-bottom: none;
        }
        
        .log-row:nth-child(odd) {
            background: #f8fbfe;
        }
        .log-row:hover {
            background: #eaf4fe;
        }
        
        .log-row::before {
            content: '';
            position: absolute;
            left: 0;
            top: 0;
            bottom: 0;
            width: 2px;
            background: var(--color-prim)HTML" \
R"HTML(ary);
            opacity: 0;
            transition: opacity var(--duration-fast) var(--easing-standard);
        }
        .log-row:hover::before {
            opacity: 1;
        }
        .log-cell {
            min-width: 0;
            overflow: hidden;
            white-space: nowrap;
            text-overflow: ellipsis;
        }
        .log-cell-index {
            text-align: right;
            font-size: 9.5px;
            color: var(--color-text-tertiary);
            font-variant-numeric: tabular-nums;
        }
        .log-cell-time {
            text-align: left;
            font-size: 9.5px;
            color: var(--color-text-secondary);
            font-family: Consolas, 'Cascadia Mono', ui-monospace, monospace;
            font-variant-numeric: tabular-nums;
        }
        .log-cell-text {
            text-align: left;
        }

        
        .log-empty {
            min-height: 100%;
            box-sizing: border-box;
            display: flex;
           )HTML" \
R"HTML( flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 5px;
            color: var(--color-text-tertiary);
            font-size: 10px;
            user-select: none;
        }
        .log-empty svg {
            width: 18px;
            height: 18px;
            fill: none;
            stroke: currentColor;
            stroke-width: 1.2;
            stroke-linecap: round;
            stroke-linejoin: round;
            opacity: 0.45;
        }

        #wpe button {
            position: relative;
            will-change: transform, box-shadow, filter;
            transform: translateZ(0);
            transition: transform var(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), filter var(--duration-fast) var(--easing-standard);
        }

        #wpe button:hover {
            transform: translateY(-1px);
            box-shadow: 0 3px 10px rgba(0, 0, 0, 0.08);
            filter: b)HTML" \
R"HTML(rightness(1.02);
        }

        #wpe button:active {
            transform: translateY(0);
            box-shadow: none;
            filter: brightness(0.98);
        }

        @media (prefers-reduced-motion: reduce) {
            *, *::before, *::after {
                animation-duration: 0.01ms !important;
                animation-iteration-count: 1 !important;
                transition-duration: 0.01ms !important;
                scroll-behavior: auto !important;
            }

            body::before {
                animation: none !important;
            }
        }
    </style>
</head>
<body>

<div style="position:absolute;top:32px;left:0;right:0;height:566px;display:flex;align-items:center;
justify-content:center;background:#eef3fa;border-bottom:1px solid #e0e0e0;color:#6e6e6e;font-size:12px;">
游戏画面区域（基线对照 · 改版前）</div>

    
    <div class="title-bar">
        <div class="title-bar-left">
            <div class="title-bar-icon">卡</div>
            <span class="title-b)HTML" \
R"HTML(ar-title">卡布西游浮影微端 V1.17</span>
        </div>
        <div class="title-bar-controls">
            <button class="title-bar-button" id="btn-minimize" title="最小化">
                <svg viewBox="0 0 12 12">
                    <rect x="2" y="5.5" width="8" height="1" fill="currentColor"/>
                </svg>
            </button>
            <button class="title-bar-button close" id="btn-close" title="关闭">
                <svg viewBox="0 0 12 12">
                    <path d="M2 2L10 10M10 2L2 10" stroke="currentColor" stroke-width="1.5" fill="none"/>
                </svg>
            </button>
        </div>
    </div>

    
    
    <div class="helper-info">
        <span class="helper-label">辅助提示:</span>
        <span class="helper-text" id="helper-text">当前暂时没有操作</span>
    </div>

    
    <div class="update-dialog-overlay" id="update-dialog-overlay">
        <div class="update-dialog">
            <div class="update-dialog-header">
                <div class="update-dialog-head)HTML" \
R"HTML(er-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
                        <polyline points="7 10 12 15 17 10"/>
                        <line x1="12" y1="15" x2="12" y2="3"/>
                    </svg>
                </div>
                <span>发现新版本</span>
            </div>
            <div class="update-dialog-body">
                <div class="update-dialog-announcement" id="update-dialog-announcement"></div>
                <div class="update-dialog-content" id="update-dialog-content"></div>
            </div>
            <div class="update-dialog-footer">
                <button class="update-dialog-btn secondary" id="update-dialog-cancel">稍后提醒</button>
                <button class="update-dialog-btn primary" id="update-dialog-confirm">立即更新</button>
            </div>
        </div>
    </div>

    
    <div class="update-dialog-overlay" id="key-login-overlay">
        <div class="update-dialog)HTML" \
R"HTML(">
            <div class="update-dialog-header">
                <div class="update-dialog-header-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M8 1a4 4 0 0 0-4 4 4 4 0 0 0 3.5 3.97V11h-1a1 1 0 0 0-1 1v2a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1v-2.03A4 4 0 0 0 12 5a4 4 0 0 0-4-4zm0 1a3 3 0 0 1 3 3 3 3 0 0 1-3 3 3 3 0 0 1-3-3 3 3 0 0 1 3-3z"/>
                    </svg>
                </div>
                <span>Key 登录</span>
            </div>
            <div class="update-dialog-body">
                <div class="update-dialog-content">
                    <textarea id="key-login-input" placeholder="粘贴登录 Key..." style="width: 100%; min-height: 120px; padding: 12px; border: 2px solid var(--color-border); border-radius: var(--radius-md); font-family: 'Consolas', 'Monaco', 'Courier New', monospace; font-size: 12px; line-height: 1.5; color: var(--color-text-primary); background: var(--color-background); resize: vertical; transition: all var(--duration-fast)HTML" \
R"HTML() var(--easing-standard); outline: none;"></textarea>
                </div>
            </div>
            <div class="update-dialog-footer">
                <button class="update-dialog-btn secondary" id="key-login-cancel">取消</button>
                <button class="update-dialog-btn primary" id="key-login-confirm">登录</button>
            </div>
        </div>
    </div>

    <div class="update-dialog-overlay" id="spirit-confirm-overlay">
        <div class="update-dialog">
            <div class="update-dialog-header">
                <div class="update-dialog-header-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M12 2L2 7l10 5 10-5-10-5z"/>
                        <path d="M2 17l10 5 10-5"/>
                        <path d="M2 12l10 5 10-5"/>
                    </svg>
                </div>
                <span>确认赠送</span>
            </div>
            <div class="update-dialog-body">
                <div class="update-dialog-announcement" i)HTML" \
R"HTML(d="spirit-confirm-title">精魄赠送确认</div>
                <div class="update-dialog-content" id="spirit-confirm-message"></div>
            </div>
            <div class="update-dialog-footer">
                <button class="update-dialog-btn secondary" id="spirit-confirm-cancel">取消</button>
                <button class="update-dialog-btn primary" id="spirit-confirm-accept">确认赠送</button>
            </div>
        </div>
    </div>

    <div class="container">
        
        
        <div id="packet-window" style="position: absolute; top: -294px; left: 0; width: 420px; height: 294px; background: var(--color-background); border: 1px solid var(--color-border); border-radius: var(--radius-md); box-shadow: var(--shadow-xl); z-index: 9999; display: none; opacity: 0; transform: translateY(0); transition: opacity 0.2s ease, transform 0.2s ease; border-top: 3px solid var(--color-primary);">
            <div style="display: flex; justify-content: space-between; align-items: center; padding: 5.6p)HTML" \
R"HTML(x 11.2px; background: var(--color-background-secondary); border-bottom: 1px solid var(--color-border); border-radius: var(--radius-md) var(--radius-md) 0 0;">
                <h3 style="font-size: 11.2px; font-weight: 600; color: var(--color-text-primary); margin: 0;">封包列表</h3>
                <div style="display: flex; gap: 2.8px;">
                    <button id="delete-selected-packets" style="padding: 2.8px 5.6px; font-size: 8.4px; background: #ffebee; color: #c62828; border: 1px solid #ffcdd2; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">删除选中</button>
                    <button id="close-packet-window" style="padding: 2.8px 5.6px; font-size: 8.4px; background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">关闭</button>
                </div>
            </div>
            <div class="packet-list" id="p)HTML" \
R"HTML(acket-list" style="display: flex; flex-direction: column;">
                
                <div id="packet-list-items" style="flex: 1; overflow-y: auto; overflow-x: hidden; padding: 2.8px;">
                    
                </div>
            </div>
            
            <div class="packet-send-container" style="padding: 5.6px; gap: 4.2px;">
                <input type="text" class="packet-hex-input" id="packet-hex-input" placeholder="选中封包后显示16进制字符串，可编辑后发送" style="font-size: 8.4px; padding: 2.8px 5.6px;">
                <button class="send-packet-btn" id="send-packet-btn" style="padding: 2.8px 8.4px; font-size: 8.4px;">发送</button>
            </div>
        </div>
        
        <div class="tabs">
            <button class="tab-button active" onclick="openTab(event, 'battle')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M8 2v7.8"/><path d="M5.2 9.8h5.6"/><path d="M8 9.8V13"/><path d="M6.9 13h2.2"/></svg></span><span>战斗数据</span)HTML" \
R"HTML(></button>
            <button class="tab-button" onclick="openTab(event, 'monster')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M4.6 5.6h6.8A1.6 1.6 0 0 1 13 7.2v4.8a1.6 1.6 0 0 1-1.6 1.6H4.6A1.6 1.6 0 0 1 3 12V7.2a1.6 1.6 0 0 1 1.6-1.6z"/><path d="M6.2 5.6V4.4a1.8 1.8 0 0 1 3.6 0v1.2"/><path d="M6 9.6h4"/></svg></span><span>妖怪背包</span></button>
            <button class="tab-button" onclick="openTab(event, 'daily')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><rect x="3" y="3" width="4.2" height="4.2" rx="1.1"/><rect x="8.8" y="3" width="4.2" height="4.2" rx="1.1"/><rect x="3" y="8.8" width="4.2" height="4.2" rx="1.1"/><rect x="8.8" y="8.8" width="4.2" height="4.2" rx="1.1"/></svg></span><span>功能区</span></button>
            <button class="tab-button" onclick="openTab(event, 'wpe')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><circle cx="8" cy="3.6" r="1.5"/>)HTML" \
R"HTML(<circle cx="3.6" cy="12.2" r="1.5"/><circle cx="12.4" cy="12.2" r="1.5"/><path d="M7.2 4.9L4.4 10.8"/><path d="M8.8 4.9l2.8 5.9"/><path d="M5.1 12.2h5.8"/></svg></span><span>WPE</span></button>
            <button class="tab-button" onclick="openTab(event, 'settings')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M2.6 4.6h10.8"/><path d="M2.6 8h10.8"/><path d="M2.6 11.4h10.8"/><circle cx="6" cy="4.6" r="1.3"/><circle cx="10.2" cy="8" r="1.3"/><circle cx="5.4" cy="11.4" r="1.3"/></svg></span><span>设置</span></button>
            <button class="tab-button" onclick="openTab(event, 'helper-log')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M4.4 2.4h4.9l2.9 2.9v8.3a.9.9 0 0 1-.9.9H4.4a.9.9 0 0 1-.9-.9V3.3a.9.9 0 0 1 .9-.9z"/><path d="M9.3 2.4v2.9h2.9"/><path d="M5.9 8.7h4.2"/><path d="M5.9 11.2h4.2"/></svg></span><span>辅助日志</span></button>

            
            <div class="tab-actions">
       )HTML" \
R"HTML(         
                <button class="tab-action-button" id="copy-key" title="复制登录Key">
                    <span class="icon-copy">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <rect x="5.4" y="5.4" width="8.2" height="8.2" rx="1.6"/>
                            <path d="M10.6 5.4V4.4A1.6 1.6 0 0 0 9 2.8H4.4A1.6 1.6 0 0 0 2.8 4.4V9A1.6 1.6 0 0 0 4.4 10.6h1"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">复制Key</span>
                </button>

                
                <button class="tab-action-button" id="key-login" title="使用Key登录">
                    <span class="icon-key">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <circle cx="5.3" cy="8" r="2.7"/>
                            <path d="M8 8h5.6"/>
                            <path d="M11.2 8v2.3"/>
                         )HTML" \
R"HTML(   <path d="M13.6 8v1.7"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">Key登录</span>
                </button>

                
                <button class="tab-action-button" id="refresh-game" title="刷新游戏">
                    <span class="icon-refresh">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M13 8A5 5 0 1 1 10.9 3.9"/>
                            <path d="M13.2 2.7v3.1h-3.1"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">刷新游戏</span>
                </button>

                
                <button class="tab-action-button" id="refresh-no-login" title="不换号刷新">
                    <span class="icon-refresh-nologin">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M11.6 6.9a4.3 4.3 0 1 0 .3 2.6"/>
  )HTML" \
R"HTML(                          <path d="M12.3 3.4v3.1h-3.1"/>
                            <path d="M10.9 9.3h2.4a.8.8 0 0 1 .8.8v1.7a.8.8 0 0 1-.8.8h-2.4a.8.8 0 0 1-.8-.8v-1.7a.8.8 0 0 1 .8-.8z"/>
                            <path d="M11 9.3v-.8a1.1 1.1 0 0 1 2.2 0v.8"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">不换号刷新</span>
                </button>
                
                
                <button class="tab-action-button" id="mute-game" title="游戏静音/恢复">
                    <span class="icon-mute">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M2.7 6.4h2.2L8.4 3.5v9l-3.5-2.9H2.7z"/>
                            <g class="mute-wave">
                                <path d="M10.6 6.3a2.5 2.5 0 0 1 0 3.4"/>
                                <path d="M12.5 4.9a4.6 4.6 0 0 1 0 6.2"/>
                            </g>
                            <p)HTML" \
R"HTML(ath class="mute-slash" d="M10.2 5.7l4 4.6"/>
                        </svg>
                    </span>
                    <span class="tab-action-label" id="mute-text">游戏静音</span>
                </button>

                
                <button class="tab-action-button" id="clear-ie-cache" title="清理 IE 浏览器缓存">
                    <span class="icon-clear-cache">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M3.1 4.9h9.8"/>
                            <path d="M6.3 4.9V3.5a.9.9 0 0 1 .9-.9h1.6a.9.9 0 0 1 .9.9v1.4"/>
                            <path d="M4.5 4.9l.6 7.3a1 1 0 0 0 1 .9h3.8a1 1 0 0 0 1-.9l.6-7.3"/>
                            <path d="M6.8 7.3v3.8"/>
                            <path d="M9.2 7.3v3.8"/>
                        </svg>
                    </span>
                    <span class="tab-action-label" id="clear-cache-text">清理缓存</span>
                </button>
            </div>
       )HTML" \
R"HTML( </div>
        <div class="content">
            
            <div id="battle" class="tab-content active">
                <div class="flat-pane">
                    
                    <section class="bp-side bp-ally">
                        <div class="flat-head">
                            <span class="flat-dot ally"></span>
                            <span class="flat-title">我方</span>
                            <select id="my-pet-select" class="bp-select">
                                <option value="0">妖怪1</option>
                            </select>
                        </div>
                        <div class="bp-idline">
                            <span id="my-name" class="bp-name">未知</span>
                            <span class="bp-sid">ID <b id="my-spirit-id">0</b></span>
                            <span id="my-hp" class="bp-hp">0/0</span>
                        </div>
                        <div id="my-buf-list" class="bp-bufs"></div>
                   )HTML" \
R"HTML(     <div class="bp-skills">
                            <div class="bp-skill-head"><span>技能</span><span>PP</span></div>
                            <div id="my-skill-list" class="bp-skill-body">
                                <div class="flat-empty">暂无技能数据</div>
                            </div>
                        </div>
                    </section>

                    <div class="flat-sep"></div>

                    
                    <section class="bp-side bp-enemy">
                        <div class="flat-head">
                            <span class="flat-dot enemy"></span>
                            <span class="flat-title">敌方</span>
                            <select id="other-pet-select" class="bp-select">
                                <option value="0">敌方妖怪1</option>
                            </select>
                        </div>
                        <div class="bp-idline">
                            <span id="other-name" class="bp-name">未知</span>
)HTML" \
R"HTML(                            <span class="bp-sid">ID <b id="other-spirit-id">0</b></span>
                            <span id="other-hp" class="bp-hp">0/0</span>
                        </div>
                        <div id="other-buf-list" class="bp-bufs"></div>
                        <div class="bp-skills">
                            <div class="bp-skill-head"><span>技能</span><span>PP</span></div>
                            <div id="other-skill-list" class="bp-skill-body">
                                <div class="flat-empty">暂无技能数据</div>
                            </div>
                        </div>
                    </section>

                    <div class="flat-sep"></div>

                    
                    <section class="bp-items bp-goods">
                        <div class="flat-head">
                            <span class="flat-dot goods"></span>
                            <span class="flat-title">道具</span>
                            <div class="flat-to)HTML" \
R"HTML(ols">
                                <button class="flat-link" onclick="window.refreshPackItems()">刷新</button>
                            </div>
                        </div>
                        <div id="item-buttons-container" class="bp-items-grid">
                            <button class="item-btn item-btn-hp" data-id="100034" onclick="window.selectItem(100034, '天香丸', 170, 'hp')">
                                <span class="item-name">天香丸</span>
                                <span style="color: #d13438; font-size: 8px;">+170HP <span class="item-count" data-id="100034">(0)</span></span>
                            </button>
                            <button class="item-btn item-btn-hp" data-id="100758" onclick="window.selectItem(100758, '培元金丹', 200, 'hp')">
                                <span class="item-name">培元金丹</span>
                                <span style="color: #d13438; font-size: 8px;">+200HP <span class="item-count" data-id="100758">(0)</span></span>
    )HTML" \
R"HTML(                        </button>
                            <button class="item-btn item-btn-hp" data-id="100006" onclick="window.selectItem(100006, '大金创药', 100, 'hp')">
                                <span class="item-name">大金创药</span>
                                <span style="color: #d13438; font-size: 8px;">+100HP <span class="item-count" data-id="100006">(0)</span></span>
                            </button>
                            <button class="item-btn item-btn-hp" data-id="100031" onclick="window.selectItem(100031, '活血散', 150, 'hp')">
                                <span class="item-name">活血散</span>
                                <span style="color: #d13438; font-size: 8px;">+150HP <span class="item-count" data-id="100031">(0)</span></span>
                            </button>
                            <button class="item-btn item-btn-mp" data-id="100010" onclick="window.selectItem(100010, '小型法力剂', 5, 'mp')">
                                <span class="item-nam)HTML" \
R"HTML(e">小型法力剂</span>
                                <span style="color: #0078d4; font-size: 8px;">+5PP <span class="item-count" data-id="100010">(0)</span></span>
                            </button>
                            <button class="item-btn item-btn-mp" data-id="100011" onclick="window.selectItem(100011, '中型法力剂', 10, 'mp')">
                                <span class="item-name">中型法力剂</span>
                                <span style="color: #0078d4; font-size: 8px;">+10PP <span class="item-count" data-id="100011">(0)</span></span>
                            </button>
                            <button class="item-btn item-btn-mp" data-id="100012" onclick="window.selectItem(100012, '大型法力剂', 20, 'mp')">
                                <span class="item-name">大型法力剂</span>
                                <span style="color: #0078d4; font-size: 8px;">+20PP <span class="item-count" data-id="100012">(0)</span></span>
                            </button>
                        <)HTML" \
R"HTML(/div>
                        <div id="selected-item-hint" class="bp-hint">点击选择道具</div>
                        <div class="bp-actions">
                            <span style="flex: none; font-size: 9px; color: var(--color-text-tertiary);">数量</span>
                            <input type="number" id="item-count-input" class="bp-num" value="1" min="1" max="99">
                            <button id="buy-item-btn" class="bp-btn ghost" onclick="window.buySelectedItem()">购买</button>
                            <button id="use-item-btn" class="bp-btn" onclick="window.useSelectedItem()">使用</button>
                        </div>
                    </section>
                </div>
            </div>
            
            <div id="monster" class="tab-content">
                <div class="flat-pane">
                    
                    <section class="mb-list">
                        <div class="flat-head">
                            <span class="flat-dot bag"></span>
          )HTML" \
R"HTML(                  <span class="flat-title">妖怪列表</span>
                            <span id="monster-count" class="badge badge-success" style="padding: 0 4px; font-size: 8.5px;">0</span>
                            <div class="flat-tools">
                                <button class="flat-link" onclick="window.queryMonsters()">刷新</button>
                            </div>
                        </div>
                        <div class="mb-table">
                            <div class="mb-thead">
                                <span style="text-align: center;">#</span>
                                <span style="text-align: center;">名称</span>
                                <span style="text-align: center;">ID</span>
                                <span style="text-align: center;">性别</span>
                                <span style="text-align: center;">等级</span>
                                <span style="text-align: center;">血量</span>
                                <span )HTML" \
R"HTML(style="text-align: center;">系别</span>
                                <span style="text-align: center;">性格</span>
                                <span style="text-align: center;">资质</span>
                                <span style="text-align: left; padding-left: 2px;">技能</span>
                            </div>
                            <div id="monster-list" style="overflow-y: auto; flex: 1; min-height: 0;">
                                <div class="flat-empty">暂无妖怪数据，请打开游戏妖怪背包</div>
                            </div>
                        </div>
                    </section>

                    <div class="flat-sep"></div>

                    
                    <section class="mb-detail">
                        <div class="flat-head">
                            <span class="flat-dot bag"></span>
                            <span class="flat-title">妖怪详情</span>
                        </div>
                        <div id="monster-detail" class="mb-detail-body">
    )HTML" \
R"HTML(                        <div class="flat-empty">点击左侧妖怪查看详情</div>
                        </div>
                    </section>
                </div>
            </div>
            
            <div id="settings" class="tab-content">
                <div style="padding: 10px; display: flex; flex-direction: row; gap: 12px; align-items: flex-start; justify-content: center;">
                    
                    <div class="panel-animate" style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 12px; box-shadow: var(--shadow-sm); width: 180px; flex-shrink: 0;">
                        <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px;">
                            <span style="font-size: 13px; font-weight: 600; color: var(--color-text-primary);">变速</span>
                            <span id="speed-value" style="font-size: 12px; font-weight: 500; color: var(--color-primary); backgr)HTML" \
R"HTML(ound: rgba(0, 120, 212, 0.1); padding: 3px 8px; border-radius: 10px;">1.0X</span>
                        </div>

                        <div style="position: relative; height: 24px; display: flex; align-items: center; justify-content: center;">
                            <input type="range" id="speed-slider" min="1" max="5" step="1" value="1"
                                style="width: 120px; cursor: pointer; accent-color: var(--color-primary); transition: all 0.2s ease;">
                        </div>

                        <div style="display: flex; justify-content: space-between; margin-top: 6px;">
                            <span style="font-size: 10px; color: var(--color-text-tertiary);">1X</span>
                            <span style="font-size: 10px; color: var(--color-text-tertiary);">5X</span>
                        </div>
                    </div>

                    
                    <div class="panel-animate" style="background: var(--color-background); bord)HTML" \
R"HTML(er: 1px solid var(--color-border); border-radius: 4px; padding: 12px; box-shadow: var(--shadow-sm); width: 140px; flex-shrink: 0; height: 99px; display: flex; flex-direction: column;">
                        <div style="font-size: 13px; font-weight: 600; color: var(--color-text-primary); margin-bottom: 8px;">游戏辅助</div>

                        <div style="display: flex; flex-direction: column; gap: 2px; justify-content: center; flex: 1;">
                            <label style="display: flex; align-items: center; gap: 6px; cursor: pointer; font-size: 10px; color: var(--color-text-primary); padding: 2px 0;">
                                <input type="checkbox" id="auto-heal" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleAutoHeal(this.checked)">
                                <span>自动回血</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 6px; cursor: pointer; font-size: 10px; color: var)HTML" \
R"HTML((--color-text-primary); padding: 2px 0;">
                                <input type="checkbox" id="battlesix-auto-battle" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleBattleSixAutoBattle(this.checked)">
                                <span>自动战斗</span>
                            </label>
                        </div>
                    </div>
                </div>
            </div>
            
            <div id="helper-log" class="tab-content">
                <div class="log-panel">
                    
                    <div class="log-toolbar">
                        <div class="log-toolbar-left">
                            <span class="log-status-dot"></span>
                            <span class="log-count" id="helper-log-count">共 <b>0</b> 条</span>
                        </div>
                        <button class="log-clear-btn" type="button" onclick="window.clearHelperLog()" title="清空全部日志">
                            <svg viewBox="0 0 16)HTML" \
R"HTML( 16" xmlns="http://www.w3.org/2000/svg">
                                <path d="M3.1 4.9h9.8"/>
                                <path d="M6.3 4.9V3.5a.9.9 0 0 1 .9-.9h1.6a.9.9 0 0 1 .9.9v1.4"/>
                                <path d="M4.5 4.9l.6 7.3a1 1 0 0 0 1 .9h3.8a1 1 0 0 0 1-.9l.6-7.3"/>
                            </svg>
                            <span>清空</span>
                        </button>
                    </div>

                    
                    <div class="log-table">
                        <div class="log-body" id="helper-log-list">
                            <div class="log-thead">
                                <div class="log-cell log-cell-index">#</div>
                                <div class="log-cell log-cell-time">时间</div>
                                <div class="log-cell log-cell-text">内容</div>
                            </div>
                            <div class="log-empty">
                                <svg viewBox="0 0 16 16" xm)HTML" \
R"HTML(lns="http://www.w3.org/2000/svg">
                                    <path d="M4.4 2.4h4.9l2.9 2.9v8.3a.9.9 0 0 1-.9.9H4.4a.9.9 0 0 1-.9-.9V3.3a.9.9 0 0 1 .9-.9z"/>
                                    <path d="M9.3 2.4v2.9h2.9"/>
                                    <path d="M5.9 8.7h4.2"/>
                                    <path d="M5.9 11.2h4.2"/>
                                </svg>
                                <span>暂无日志记录</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            
            <div id="daily" class="tab-content">
                
                <div class="sub-tabs">
                    <button class="sub-tab-button active" onclick="openSubTab(event, 'sub-daily')">日常活动</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-taskzone')">任务区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'su)HTML" \
R"HTML(b-latest')">最新活动</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-materials')">材料采集</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-boss')">BOSS专区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-lingyu')">灵玉区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-dungeon')">副本区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-battlesix')" style="display: none;">万妖盛会</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-spiritcollect')">精魄系统</button>
                </div>

                
                <div id="sub-daily" class="sub-tab-content active">
                    <div class="fx-page">
                        
                        <div class="fx-row">

                            
                            <div class="fx-pan)HTML" \
R"HTML(el" style="flex: 2.2;">
                                <span class="fx-panel-title">每日</span>
                                <div class="fx-checks" style="grid-template-columns: repeat(4, minmax(0, 1fr));">
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-deep-dig" checked style="width: 11px; height: 11px;">
                                        <span>深度挖宝</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="family-checkin" checked style="width: 11px; height: 11px;">
                                        <span>家族考勤</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-card" checked style="width: 11px; height: 11px;">
                         )HTML" \
R"HTML(               <span>每日卡牌</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="family-report" checked style="width: 11px; height: 11px;">
                                        <span>家族报道</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-gift" checked style="width: 11px; height: 11px;">
                                        <span>每日礼包</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="family-defend" checked style="width: 11px; height: 11px;">
                                        <span>家族保卫</span>
                                    </label>
                                    <label class="fx-ch)HTML" \
R"HTML(eck">
                                        <input type="checkbox" id="weekly-gift" checked style="width: 11px; height: 11px;">
                                        <span>每周礼包</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="shop-surprise" checked style="width: 11px; height: 11px;">
                                        <span>商城惊喜</span>
                                    </label>
                                </div>
                                <div class="fx-actions">
                                    <button id="complete-daily-tasks" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 8px; width: 100%;">一键完成</button>
                                </div>
                            </div>

                            
                            
                            <div class="fx-panel" style="display: none; flex: 1 1 3)HTML" \
R"HTML(00px; min-width: 250px;">
                                <span class="fx-panel-title">活动</span>
                                <div class="fx-checks" style="grid-template-columns: repeat(2, minmax(0, 1fr));">
                                    <label class="fx-check">
                                        <input type="checkbox" id="act-run-competition" checked style="width: 11px; height: 11px;">
                                        <span>障碍赛跑</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="act-jinpingfu-wish" checked style="width: 11px; height: 11px;">
                                        <span>金平府祈愿</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="act-knowledge-garden" checked style="width: 11px; height: 11px;">
   )HTML" \
R"HTML(                                     <span>知识小天地</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="act-gomoku" style="width: 11px; height: 11px;">
                                        <span>五子棋</span>
                                    </label>
                                </div>
                                <div class="fx-actions">
                                    <button id="complete-activity-tasks" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 8px; width: 100%;">一键活动</button>
                                </div>
                            </div>

                            
                            <div class="fx-panel">
                                <span class="fx-panel-title">试炼</span>
                                <div class="fx-checks" style="grid-template-columns: 1fr;">
                                    <la)HTML" \
R"HTML(bel class="fx-check">
                                        <input type="checkbox" id="daily-fire-wind" style="width: 11px; height: 11px;">
                                        <span>火风试炼</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-fire" style="width: 11px; height: 11px;">
                                        <span>火焰试炼</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-storm" style="width: 11px; height: 11px;">
                                        <span>风暴试炼</span>
                                    </label>
                                </div>
                                <div class="fx-actions">
                                    <button id="complete-trial-tasks" class="btn btn-primary" style=)HTML" \
R"HTML("font-size: 9.5px; padding: 3px 8px; width: 100%;">一键试炼</button>
                                </div>
                            </div>

                            
                            <div class="fx-panel">
                                <span class="fx-panel-title">跳舞大赛</span>
                                <label class="fx-check">
                                    <input type="checkbox" id="daily-dance" style="width: 11px; height: 11px;">
                                    <span>自动跳舞</span>
                                </label>
                                <div style="font-size: 9px; color: var(--color-text-secondary); flex: 0 0 auto;">已完成: <span id="dance-count" style="color: var(--color-primary); font-weight: 600;">0</span>/3 次</div>
                                <div class="fx-actions">
                                    <button id="complete-dance-tasks" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 6px; width: 100%;">一键跳舞</button>
      )HTML" \
R"HTML(                          </div>
                            </div>

                            
                            <div class="fx-panel" style="flex: 1.3;">
                                <span class="fx-panel-title">玄塔</span>
                                <div class="fx-actions" style="margin-top: 0;">
                                    <button id="one-key-xuantta" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 4px;">一键玄塔</button>
                                    <button id="buy-dice-18" class="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 4px;">购买骰子18</button>
                                </div>
                                <label class="fx-check" style="flex: 0 0 auto;">
                                    <input type="checkbox" id="auto-go-home" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleAutoGoHome(this.checked)">
                                    <span>自动回家</span>
                       )HTML" \
R"HTML(         </label>
                                <label class="fx-check" style="flex: 0 0 auto;">
                                    <input type="checkbox" id="block-battle" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleBlockBattle(this.checked)">
                                    <span>屏蔽战斗</span>
                                </label>
                            </div>

                            
                            <div class="fx-panel" style="flex: 1.2;">
                                <span class="fx-panel-title">福瑞宝箱</span>
                                <div class="fx-panel-note">自动福瑞 11:00-13:00 / 17:00-19:00</div>
                                <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                                    <label style="font-size: 9.5px; color: var(--color-text-secondary);">目标:</label>
                                    <input type="number" id="heaven-furui-max" value="30" min="1" max="3)HTML" \
R"HTML(0" style="width: 40px; font-size: 9.5px; padding: 2px 4px; border: 1px solid var(--color-border); border-radius: 2px;">
                                    <span style="font-size: 9.5px; color: var(--color-text-secondary);">个</span>
                                </div>
                                <div style="font-size: 9.5px; color: var(--color-text-secondary); flex: 0 0 auto;">已拾取: <span id="heaven-furui-count" style="color: var(--color-primary); font-weight: 600;">0</span>/<span id="heaven-furui-target">30</span></div>
                                <div class="fx-actions">
                                    <button id="start-heaven-furui" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 6px; flex: 1;">开始</button>
                                    <button id="stop-heaven-furui" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 6px; flex: 1;" disabled>停止</button>
                                </div>
                            </div>

           )HTML" \
R"HTML(                 
                            <div class="fx-panel" style="flex: 1.3;">
                                <span class="fx-panel-title">坐骑大赛</span>
                                <div class="fx-panel-note">多人竞速 · 每日50点上限 · 固定临时坐骑</div>
                                <div style="font-size: 9.5px; color: var(--color-text-secondary); flex: 0 0 auto;">可获得积分: <span id="horse-competition-points" style="color: var(--color-primary); font-weight: 600;">--</span></div>
                                <div style="font-size: 9.5px; color: var(--color-text-secondary); flex: 0 0 auto;">状态: <span id="horse-competition-status" style="color: var(--color-text-tertiary);">就绪</span></div>
                                <div class="fx-actions">
                                    <button id="one-key-horse-competition" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 6px; flex: 1;">开始</button>
                                    <button id="stop-horse-competition" class="btn" st)HTML" \
R"HTML(yle="font-size: 9.5px; padding: 3px 6px; flex: 1; background: #fff4e5; color: #9a5b00; border: 1px solid #ffd08a;">停止</button>
                                </div>
                            </div>

                        </div>
                        
                        <div id="daily-task-status" style="font-size: 9px; color: var(--color-text-secondary); padding: 2px 6px; background: var(--color-background-secondary); border-radius: 2px; text-align: center; flex: 0 0 auto;">
                            勾选需要完成的日常活动，点击对应按钮执行
                        </div>
                    </div>
                </div>

                <div id="sub-taskzone" class="sub-tab-content">
                    <div class="fx-row" style="padding: 4px 6px; flex-wrap: wrap;">

                        
                        <div class="fx-panel" style="flex: 0 0 240px;">
                            <span class="fx-panel-title">八卦灵盘</span>
                            <div class="fx-panel-note" style=")HTML" \
R"HTML(white-space: normal;">按任务链顺序推进，覆盖对话、采集与场景切换，最终战斗自动处理。</div>
                            <div class="fx-actions">
                                <button id="start-task-zone" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;">开始</button>
                                <button id="stop-task-zone" class="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;" disabled>停止</button>
                            </div>
                            <div id="task-zone-status" style="font-size: 9px; color: var(--color-text-secondary); background: var(--color-background-secondary); border-radius: 2px; padding: 2px 6px; flex: 0 0 auto;">等待开始</div>
                        </div>

                            
                            <div id="huangcheng-weiji-panel" style="flex: 1 1 560px; min-width: 560px; background: #FFFFFF; border: 1px solid var(--color-border); border-radius: 4px; padding: 6px 8px; display: flex; flex-direction: column; gap: 4px;">
  )HTML" \
R"HTML(                              <div style="display: flex; align-items: center; flex-wrap: wrap; gap: 4px;">
                                    <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary); white-space: nowrap;">主线任务</div>
                                    <button id="refresh-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;">刷新进度</button>
                                    <button id="select-all-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;">全选本系列</button>
                                    <button id="clear-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;">清空选择</button>
                                    <button id="start-huangcheng-weiji" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 8px;">完成所选</button>
                                    <button id="stop-huangcheng-weiji" class="btn btn-secondary" style="font-size)HTML" \
R"HTML(: 9.5px; padding: 3px 8px;" disabled>停止</button>
                                    <label style="display: flex; align-items: center; gap: 4px; font-size: 9.5px; color: var(--color-text-secondary); cursor: pointer; user-select: none; margin-left: auto;">
                                        <input type="checkbox" id="huangcheng-hide-completed" checked style="width: 12px; height: 12px; cursor: pointer;">
                                        <span>隐藏已完成</span>
                                    </label>
                                </div>
                                <div id="huangcheng-weiji-groups" class="hw-groups"></div>
                                <div id="huangcheng-weiji-tasks" class="hw-tasks">
                                    <div style="font-size: 9.5px; color: var(--color-text-tertiary); padding: 4px;">正在加载主线任务列表...</div>
                                </div>
                                <div class="hw-foot">
                                    <span i)HTML" \
R"HTML(d="huangcheng-weiji-summary">已完成: 0 / 0，已勾选: 0</span>
                                    <span id="huangcheng-weiji-status">等待刷新进度</span>
                                </div>
                            </div>
                    </div>
                </div>

                <div id="sub-latest" class="sub-tab-content">
                    <div class="fx-panel" style="margin: 4px 6px; align-self: stretch; flex: 1 1 auto;">
                        
                        <div style="display: flex; align-items: center; justify-content: space-between; gap: 8px; flex: 0 0 auto;">
                            <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary);">活动选择</div>
                            <div id="act-latest-hint" style="font-size: 9px; color: var(--color-text-tertiary);">勾选要执行的活动，点击“一键执行”；扫荡自动判定</div>
                        </div>

                        
                        <div style="display: grid; grid-template-columns: repeat(auto-fill)HTML" \
R"HTML(, minmax(158px, 1fr)); gap: 2px 6px; flex: 1 1 auto; min-height: 0; align-content: start; overflow-y: auto;">
                            <label class="act-item" id="act-row-647">
                                <input type="checkbox" id="act-check-647" class="act-pick">
                                <span class="act-item-name">突破迷阵</span>
                                <span class="act-item-count" id="act-count-647">--</span>
                            </label>
                            <label class="act-item" id="act-row-869">
                                <input type="checkbox" id="act-check-869" class="act-pick">
                                <span class="act-item-name">摘星之旅</span>
                                <span class="act-item-count" id="act-count-869">--</span>
                            </label>
                            <label class="act-item" id="act-row-749">
                                <input type="checkbox" id="act-check-749" class="act-pick">
      )HTML" \
R"HTML(                          <span class="act-item-name">寒冰洞历险记</span>
                                <span class="act-item-count" id="act-count-749">--</span>
                            </label>
                            <label class="act-item" id="act-row-873">
                                <input type="checkbox" id="act-check-873" class="act-pick">
                                <span class="act-item-name">填字大作战</span>
                                <span class="act-item-count" id="act-count-873">--</span>
                            </label>
                            <label class="act-item" id="act-row-742">
                                <input type="checkbox" id="act-check-742" class="act-pick">
                                <span class="act-item-name">守护葫芦藤</span>
                                <span class="act-item-count" id="act-count-742">--</span>
                            </label>
                            <label class="act-item" id="act-row-721">
           )HTML" \
R"HTML(                     <input type="checkbox" id="act-check-721" class="act-pick">
                                <span class="act-item-name">采菊东篱下</span>
                                <span class="act-item-count" id="act-count-721">--</span>
                            </label>
                            <label class="act-item" id="act-row-877">
                                <input type="checkbox" id="act-check-877" class="act-pick">
                                <span class="act-item-name">寻找七色茱萸</span>
                                <span class="act-item-count" id="act-count-877">--</span>
                            </label>
                            <label class="act-item" id="act-row-730">
                                <input type="checkbox" id="act-check-730" class="act-pick">
                                <span class="act-item-name">智斗恶灵</span>
                                <span class="act-item-count" id="act-count-730">--</span>
                            </la)HTML" \
R"HTML(bel>
                            <label class="act-item" id="act-row-880">
                                <input type="checkbox" id="act-check-880" class="act-pick">
                                <span class="act-item-name">夺宝行动</span>
                                <span class="act-item-count" id="act-count-880">--</span>
                            </label>
                        </div>

                        
                        <div style="display: flex; align-items: center; gap: 6px; margin-top: 2px; flex: 0 0 auto;">
                            <button onclick="selectAllActivities(true)" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">全选</button>
                            <button onclick="selectAllActivities(false)" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">全不选</button>
                            <button onclick="refreshActivityCounts()" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">刷新次数</butt)HTML" \
R"HTML(on>
                            <button onclick="stopSelectedActivities()" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">停止</button>
                            <button onclick="runSelectedActivities()" class="btn btn-primary" style="font-size: 10.5px; padding: 3px 14px; margin-left: auto;">一键执行所选</button>
                        </div>
                    </div>
                </div>

                <div id="sub-materials" class="sub-tab-content">
                    <div class="fx-panel" style="margin: 4px 6px; flex: 0 1 auto; align-self: stretch;">
                        
                        <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary); flex: 0 0 auto;">每日采集</div>

                        
                        <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(112px, 1fr)); gap: 2px 4px; flex: 0 0 auto;">
                            
                            <label style="display: flex; a)HTML" \
R"HTML(lign-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-0" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>上清宝玉</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-1" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>天地灵气</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-2" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cur)HTML" \
R"HTML(sor: pointer;">
                                <span>火云岩</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-3" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>千年红木</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-4" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>远古青松</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; )HTML" \
R"HTML(color: #333333;">
                                <input type="checkbox" id="collect-5" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>精蓝石</span>
                            </label>
                            
                            
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-6" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>松绿石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-7" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: point)HTML" \
R"HTML(er;">
                                <span>冰霜岩</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-8" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>幻影石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-9" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>冰晶砂</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #3333)HTML" \
R"HTML(33;">
                                <input type="checkbox" id="collect-10" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>天罡石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-11" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>茅山玉</span>
                            </label>
                            
                            
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-12" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
   )HTML" \
R"HTML(                             <span>神明果</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-13" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>朱砂石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-14" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>青金石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
 )HTML" \
R"HTML(                               <input type="checkbox" id="collect-15" style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>坐骑蛋</span>
                            </label>
                        </div>
                        
                        
                        <div style="display: flex; justify-content: flex-end; gap: 6px; margin-top: 4px; flex: 0 0 auto;">
                            <button onclick="selectAllCollect(true)" style="background: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 3px; padding: 3px 10px; font-size: 10px; font-weight: 500; cursor: pointer; transition: all 0.2s;" onmouseover="this.style.background='#F5F5F5'" onmouseout="this.style.background='#FFFFFF'">全选</button>
                            <button onclick="selectAllCollect(false)" style="background: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 3px; padding: 3px 10px; font-size: 10px; font-w)HTML" \
R"HTML(eight: 500; cursor: pointer; transition: all 0.2s;" onmouseover="this.style.background='#F5F5F5'" onmouseout="this.style.background='#FFFFFF'">全不选</button>
                            <button onclick="startOneKeyCollect()" style="background: var(--color-primary); color: #FFFFFF; border: none; border-radius: 3px; padding: 3px 12px; font-size: 10.5px; font-weight: 600; cursor: pointer; transition: background 0.2s;" onmouseover="this.style.background='var(--color-primary-hover)'" onmouseout="this.style.background='var(--color-primary)'" onmousedown="this.style.background='var(--color-primary-pressed)'" onmouseup="this.style.background='var(--color-primary-hover)'">一键采集</button>
                        </div>
                    </div>
                </div>

                <div id="sub-boss" class="sub-tab-content">
                    
                    <div style="display: flex; flex-direction: column; gap: 8px; padding: 8px;">
                        <div class="panel-animate" style)HTML" \
R"HTML(="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 10px; box-shadow: var(--shadow-sm); width: 100%; max-width: 340px; min-width: 280px;">
                            <div style="font-size: 11.5px; font-weight: 600; color: var(--color-text-primary); margin-bottom: 8px;">boss对战</div>
                            
                            
                            <div style="display: flex; gap: 6px; align-items: center; min-width: 0;">
                                
                                <div style="display: flex; gap: 4px; flex: 1; min-width: 0;">
                                    <input type="text" id="boss-search-input" placeholder="搜索..."
                                        style="width: 72px; padding: 5px 8px; font-size: 10.5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); outline: none; transition: all var(--duration-normal) var(--easing-standard); background: var(--color-backg)HTML" \
R"HTML(round); color: var(--color-text-primary);"
                                        oninput="filterBossSelect()">
                                    <select id="boss-select"
                                        style="flex: 1; min-width: 0; padding: 5px 10px; font-size: 10.5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); outline: none; transition: all var(--duration-normal) var(--easing-standard); background: var(--color-background); color: var(--color-text-primary); cursor: pointer;"
                                        onchange="selectBoss(this.value)">
                                        <option value="0">-- 请选择BOSS --</option>
                                        
                                    </select>
                                </div>
                                
                                
                                <button id="enter-boss-battle-btn" onclick="enterBossBattle()" 
                                   )HTML" \
R"HTML( style="flex: 0 0 auto; padding: 4px 10px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: 2px; cursor: pointer; transition: all 0.2s linear; white-space: nowrap;"
                                    onmouseover="this.style.background='var(--color-primary-hover)'" 
                                    onmouseout="this.style.background='var(--color-primary)'">
                                    进入战斗
                                </button>
                            </div>
                            
                            
                            <div id="selected-boss-info" style="margin-top: 8px; font-size: 9.5px; color: var(--color-text-secondary);">
                                请选择一个BOSS
                            </div>
                        </div>
                    </div>
                </div>

                <div id="sub-lingyu" class="sub-tab-content">
                    <div class="fx-row">
                  )HTML" \
R"HTML(      
                        <div class="fx-panel" style="flex: 1.15;">
                            <div style="display: flex; justify-content: space-between; align-items: center; gap: 6px; flex: 0 0 auto;">
                                <div style="display: flex; align-items: center; gap: 5px; min-width: 0;">
                                    <span class="fx-panel-title">灵玉列表</span>
                                    <div id="lingyu-count" class="badge badge-success" style="padding: 0 6px; font-size: 10px;">0</div>
                                </div>
                                
                                <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                                    <select id="lingyu-sort-attr" style="font-size: 10px; padding: 1px 4px; border: 1px solid var(--color-border); border-radius: 2px; outline: none; height: 19px; background: var(--color-background);">
                                        <option value="n)HTML" \
R"HTML(one">不排序</option>
                                        <option value="体力">体力</option>
                                        <option value="攻击">攻击</option>
                                        <option value="防御">防御</option>
                                        <option value="法术">法术</option>
                                        <option value="抗性">抗性</option>
                                        <option value="速度">速度</option>
                                        <option value="威力">威力</option>
                                    </select>
                                    <button onclick="window.applyLingyuSort()" style="padding: 1px 8px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: 2px; cursor: pointer; height: 19px;">排序</button>
                                    <button onclick="window.queryLingyu()" style="padding: 1px 8px; font-size: 10px; background: #2e7d32; color: white; border: none; border-radius: 2px; cursor)HTML" \
R"HTML(: pointer; height: 19px;">查询灵玉</button>
                                </div>
                            </div>

                            
                            <div style="flex: 1 1 auto; min-height: 0; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                <div style="display: grid; grid-template-columns: 55px 40px 65px 1fr; background: var(--color-background-hover); padding: 2px 6px; border-bottom: 1px solid var(--color-border); font-size: 10px; font-weight: 600; color: var(--color-text-primary); flex: 0 0 auto;">
                                    <div style="text-align: center;">名称</div>
                                    <div style="text-align: center;">索引</div>
                                    <div style="text-align: center;">宠物</div>
                                    <div style="text-align: center;">附加属性</div>
                    )HTML" \
R"HTML(            </div>
                                <div id="lingyu-list" class="fx-scroll" style="font-size: 10px;">
                                    <div style="text-align: center; padding: 10px; color: var(--color-text-tertiary);">暂无数据</div>
                                </div>
                            </div>
                        </div>

                        
                        <div class="fx-panel" style="flex: 0.85;">
                            <span class="fx-panel-title" style="border-bottom: 1px solid var(--color-border); padding-bottom: 3px;">分解队列</span>

                            
                            <div style="flex: 1 1 auto; min-height: 0; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                <div style="display: grid; grid-template-columns: 45px 1fr; background: var(--color-background-hover); padding: 2px 6px; bo)HTML" \
R"HTML(rder-bottom: 1px solid var(--color-border); font-size: 10px; font-weight: 600; color: var(--color-text-primary); flex: 0 0 auto;">
                                    <div style="text-align: center;">#</div>
                                    <div style="text-align: center;">灵玉名称</div>
                                </div>
                                <div id="decompose-list" class="fx-scroll" style="font-size: 10px;">
                                    <div style="text-align: center; padding: 10px; color: var(--color-text-tertiary);">未添加灵玉</div>
                                </div>
                            </div>

                            
                            <div class="fx-actions" style="display: grid; grid-template-columns: 1fr 1fr;">
                                <button onclick="window.decomposeAllInQueue()" class="btn btn-primary" style="font-size: 10.5px; height: 21px; padding: 0; line-height: 21px;">一键分解</button>
                                <button )HTML" \
R"HTML(onclick="window.clearDecomposeQueue()" class="btn btn-secondary" style="font-size: 10.5px; height: 21px; padding: 0; line-height: 21px;">清空队列</button>
                            </div>

                            
                            <div style="font-size: 9.5px; color: var(--color-text-secondary); line-height: 1.3; flex: 0 0 auto;">
                                <span id="decompose-queue-count">已添加: 0 个灵玉</span>
                                <span style="color: var(--color-text-tertiary);"> · 分解后无法找回</span>
                            </div>
                        </div>
                    </div>
                </div>

            
            <div id="sub-dungeon" class="sub-tab-content">
                <div class="fx-row" style="padding: 4px 6px;">

                    
                    <div class="fx-panel" style="flex: 1;">
                        <span class="fx-panel-title">副本跳层</span>

                        
                        <div style="display: fl)HTML" \
R"HTML(ex; align-items: center; gap: 4px; flex: 0 0 auto;">
                            <label for="dungeon-layer-input" style="font-size: 9.5px; color: var(--color-text-secondary);">目标层数:</label>
                            <input type="number" id="dungeon-layer-input" value="1" min="1" max="9999"
                                style="width: 50px; font-size: 9.5px; padding: 2px 4px; border: 1px solid var(--color-border); border-radius: 2px;">
                            <span style="font-size: 8.5px; color: var(--color-text-tertiary);">层</span>
                        </div>

                        <div class="fx-panel-note">需50级以下妖怪</div>

                        <div class="fx-actions">
                            <button id="dungeon-jump-btn" onclick="startDungeonJump()" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;">跳层</button>
                            <button id="dungeon-stop-btn" onclick="stopDungeonJump()" class="btn btn-secondary" style="flex: 1; fo)HTML" \
R"HTML(nt-size: 9.5px; padding: 3px 6px;">停止</button>
                        </div>
                    </div>

                    
                    <div class="fx-panel" style="flex: 1.35;">
                        <span class="fx-panel-title">双台谷刷级</span>

                        <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                            <label style="font-size: 9.5px; color: var(--color-text-secondary); min-width: 35px;">主宠:</label>
                            <select id="shuangtai-pet" style="flex: 1; min-width: 0; font-size: 9.5px; padding: 2px; border: 1px solid var(--color-border); border-radius: 2px; height: 18px;" disabled>
                                <option value="">请先查询</option>
                            </select>
                            <button id="query-shuangtai" class="btn btn-secondary" style="flex: 0 0 auto; font-size: 9.5px; padding: 2px 6px;">查询</button>
                        </div>

                        <div)HTML" \
R"HTML( style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                            <label style="font-size: 9.5px; color: var(--color-text-secondary); min-width: 35px;">技能:</label>
                            <select id="shuangtai-skill" style="flex: 1; min-width: 0; font-size: 9.5px; padding: 2px; border: 1px solid var(--color-border); border-radius: 2px; height: 18px;" disabled>
                                <option value="">自动选择最高威力</option>
                            </select>
                        </div>

                        <div style="display: flex; align-items: center; gap: 6px; flex: 0 0 auto;">
                            <label class="fx-check" style="flex: 0 0 auto;">
                                <input type="checkbox" id="shuangtai-block-battle" style="width: 11px; height: 11px;">
                                <span>屏蔽战斗</span>
                            </label>
                            <span class="fx-panel-note">主宠末位 · 至少2只妖怪</span>
  )HTML" \
R"HTML(                      </div>

                        <div class="fx-actions">
                            <button id="start-shuangtai" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;" disabled>启动</button>
                            <button id="stop-shuangtai" class="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;" disabled>停止</button>
                        </div>
                    </div>

                </div>
            </div>

            
            <div id="sub-battlesix" class="sub-tab-content" style="display: none;">
                <div style="display: flex; gap: 12px; height: 100%; padding: 4px;">
                    
                    <div style="display: flex; flex-direction: column; gap: 6px; min-width: 160px;">
                        <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary);">万妖盛会PVP</div>
                        
                        
                        <div style=)HTML" \
R"HTML("display: flex; align-items: center; gap: 6px;">
                            <label for="battlesix-match-count" style="font-size: 10.5px; color: var(--color-text-primary); min-width: 70px;">匹配次数：</label>
                            <input type="number" id="battlesix-match-count" value="1" min="1" max="999" class="input" style="width: 60px; font-size: 10.5px; padding: 3px 5px;">
                        </div>

                        
                        <div style="display: flex; gap: 6px;">
                            <button id="battlesix-auto-match" class="btn btn-primary" style="font-size: 10.5px; padding: 4px 8px;">自动匹配</button>
                            <button id="battlesix-cancel-match" class="btn btn-secondary" style="font-size: 10.5px; padding: 4px 8px;">取消匹配</button>
                        </div>
                    </div>
                    
                    
                    <div style="width: 1px; background: var(--color-border);"></div>
                    )HTML" \
R"HTML(
                    
                    <div style="flex: 1;">
                        <div style="font-size: 10.5px; font-weight: 600; color: var(--color-text-secondary); margin-bottom: 4px;">功能说明</div>
                        <div style="font-size: 9.5px; color: var(--color-text-tertiary); line-height: 1.5;">
                            <div>• 自动匹配后进入战斗</div>
                            <div>• 自动战斗会自动选择技能</div>
                            <div>• 精灵死亡时自动切换</div>
                            <div>• 战斗结束后自动确认</div>
                            <div>• 支持多次自动匹配循环</div>
                        </div>
                    </div>
                </div>
            </div>

            
            <div id="sub-spiritcollect" class="sub-tab-content">
                <div class="spirit-collect-layout">
                    
                    <div class="spirit-toolbar">
                        <span class="spirit-stat-chip">今日可赠 <span id="spirit-daily-out" style="color: var(--color-primary); fo)HTML" \
R"HTML(nt-weight: 600;">-</span></span>
                        <span class="spirit-stat-chip">本周可赠 <span id="spirit-weekly-out" style="color: var(--color-primary); font-weight: 600;">-</span></span>
                        <div class="spirit-toolbar-selected">已选精魄: <span id="spirit-selected" style="color: var(--color-primary); font-weight: 600;">无</span></div>
                        <div class="spirit-toolbar-send">
                            <label>卡布号:</label>
                            <input type="text" id="spirit-friend-id" placeholder="输入对方卡布号">
                            <button onclick="verifyAndSendSpirit()" class="btn btn-primary">赠送</button>
                        </div>
                        <div class="spirit-status-line" id="spirit-status"></div>
                        <button onclick="refreshSpiritPanel()" class="btn btn-secondary">刷新</button>
                    </div>

                    
                    <div class="spirit-columns">
                        
    )HTML" \
R"HTML(                    <div class="spirit-giving-panel">
                            <div id="spirit-list">
                                <div style="color: var(--color-text-tertiary); text-align: center; padding: 10px 8px;">点击“刷新”加载精魄列表</div>
                            </div>
                        </div>

                        
                        <div class="spirit-record-side">
                            <div class="spirit-record-header">
                                <div class="spirit-record-title">记录面板</div>
                                <div class="spirit-record-meta">
                                    <div id="spirit-record-title" class="spirit-record-subtitle">等待加载</div>
                                    <div class="spirit-giving-actions">
                                        <button onclick="getSpiritRecord(1)" class="btn btn-secondary">赠送</button>
                                        <button onclick="getSpiritRecord(2)" class="btn btn-secondary">接收</bu)HTML" \
R"HTML(tton>
                                    </div>
                                </div>
                            </div>

                            <div id="spirit-record-panel" class="spirit-record-body">
                                点击上方按钮查看赠送记录或接收记录。
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            </div>

            
            <div id="wpe" class="tab-content">
                
                <div class="panel-animate" style="display: flex; gap: 10px; margin-bottom: 10px; flex-wrap: wrap; align-items: stretch;">
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 120px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6p)HTML" \
R"HTML(x;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">拦截类型</h3>
                        </div>
                        <div style="display: flex; flex-direction: column; gap: 4px;">
                            <div style="display: flex; align-items: center; gap: 5px;">
                                <input type="checkbox" id="intercept-send" checked style="width: 12px; height: 12px;">
                                <label for="intercept-send" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">发送包</label>
                            </div>
                            <div style="display: flex; align-items: center; gap: 5px;">
                                <input type="checkbox" id="intercept-receive" style="width: 12px; height: 12px;">
                                <label for="intercept-receive" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">接收包</label>
    )HTML" \
R"HTML(                        </div>
                            <div style="margin-top: 4px; display: flex; flex-direction: column; gap: 4px;">
                                <button id="start-intercept" style="padding: 4px 5px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">开始拦截</button>
                                <button id="stop-intercept" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">停止拦截</button>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div style="background: var(--color-background); bo)HTML" \
R"HTML(rder: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 140px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">发送设置</h3>
                        </div>
                        <div style="display: flex; flex-direction: column; gap: 4px;">
                            <div style="display: flex; justify-content: space-between; align-items: center;">
                                <label for="send-count" style="font-size: 10px; color: var(--color-text-primary);">发送次数:</label>
                                <input type="number" id="send-count" value="1" min="1" class="input" style="width: 40px; font-size: 10px; padding: 3px 5px;">
                            </div>
                            <div style)HTML" \
R"HTML(="display: flex; justify-content: space-between; align-items: center;">
                                <label for="send-delay" style="font-size: 10px; color: var(--color-text-primary);">发送延迟:</label>
                                <input type="number" id="send-delay" value="300" min="0" class="input" style="width: 40px; font-size: 10px; padding: 3px 5px;">
                            </div>
                            <div style="margin-top: 4px; display: flex; flex-direction: column; gap: 4px;">
                                <button id="start-send" style="padding: 4px 5px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">开始发送</button>
                                <button id="stop-send" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color)HTML" \
R"HTML(-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">停止发送</button>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 130px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">高级功能</h3>
                        </div>
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px;">
                            <button id="toggle-packet-window" style="padding: 4px 5px; font-size: 10px; bac)HTML" \
R"HTML(kground: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">显示封包</button>
                            <button id="clear-all-packets" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">清空封包</button>
                            <div style="margin: 4px 0; padding: 4px 5px; background: var(--color-background-hover); border-radius: var(--radius-sm); font-size: 10px; color: var(--color-text-secondary); text-align: center; grid-column: 1 / -1;">
                                <div>当前封包个数: <span id="packet-count">0</span></div>
                            </div>
                            <button id="save-packets" style="padding: 4px 5px; font-size: 10px; background: var(--)HTML" \
R"HTML(color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">保存封包</button>
                            <button id="load-packets" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">载入封包</button>
                        </div>
                    </div>
                    
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 240px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column; flex-shrink: 0;">
                        <div style="margin-bottom: 6px;">
                            <h3 style="font-size:)HTML" \
R"HTML( 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">劫持修改</h3>
                        </div>
                        <div style="display: flex; flex-direction: column; gap: 4px;">
                            <div style="display: flex; align-items: center; gap: 5px;">
                                <input type="checkbox" id="hijack-send" checked style="width: 12px; height: 12px;">
                                <label for="hijack-send" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">劫持发送</label>
                                <input type="checkbox" id="hijack-recv" checked style="width: 12px; height: 12px;">
                                <label for="hijack-recv" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">劫持接收</label>
                            </div>
                            <div style="display: flex; flex-direction: column; gap: 4px;">
                                <input type="text" id="hijack-p)HTML" \
R"HTML(attern" placeholder="检测文本（十六进制）" style="width: 100%; font-size: 9px; padding: 3px 5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); box-sizing: border-box;">
                                <input type="text" id="hijack-replace" placeholder="替换文本" style="width: 100%; font-size: 9px; padding: 3px 5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); box-sizing: border-box;">
                            </div>
                            <select id="hijack-type" style="width: 100%; font-size: 10px; padding: 3px 5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm);">
                                <option value="block">拦截</option>
                                <option value="replace">替换</option>
                            </select>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    <script>
        // 测试脚本是否执行
        document.title = )HTML" \
R"HTML('WebView2 Demo - Loaded';

        // 全局函数定义 - 必须在 DOM 元素之前定义
        function openTab(evt, tabName) {
            var tabContents = document.getElementsByClassName('tab-content');
            for (var i = 0; i < tabContents.length; i++) {
                tabContents[i].className = tabContents[i].className.replace(' active', '');
            }
            var tabButtons = document.getElementsByClassName('tab-button');
            for (var i = 0; i < tabButtons.length; i++) {
                tabButtons[i].className = tabButtons[i].className.replace(' active', '');
            }
            var activeTab = document.getElementById(tabName);
            activeTab.className += ' active';
            evt.currentTarget.className += ' active';
            
            if (typeof reanimateTabContent === 'function') {
                reanimateTabContent(activeTab);
            }
        }

        function openSubTab(evt, tabName) {
            var tabContents = document.getElementsByClassName(')HTML" \
R"HTML(sub-tab-content');
            for (var i = 0; i < tabContents.length; i++) {
                tabContents[i].className = tabContents[i].className.replace(' active', '');
            }
            var tabButtons = document.getElementsByClassName('sub-tab-button');
            for (var i = 0; i < tabButtons.length; i++) {
                tabButtons[i].className = tabButtons[i].className.replace(' active', '');
            }
            document.getElementById(tabName).className += ' active';
            evt.currentTarget.className += ' active';
            if (tabName === 'sub-taskzone') {
                if (typeof window.updateTaskZoneStatus === 'function') {
                    window.updateTaskZoneStatus(
                        window.isTaskZoneRunning ? '正在执行任务区...' : '等待开始',
                        window.isTaskZoneRunning
                    );
                }
                var huangchengWeijiPanel = document.getElementById('huangcheng-weiji-panel');
                if (huang)HTML" \
R"HTML(chengWeijiPanel && !huangchengWeijiPanel.hidden && typeof window.refreshHuangchengWeijiTaskProgress === 'function') {
                    window.refreshHuangchengWeijiTaskProgress();
                }
                if (huangchengWeijiPanel && !huangchengWeijiPanel.hidden && typeof window.renderHuangchengWeijiTasks === 'function') {
                    window.renderHuangchengWeijiTasks();
                }
            }
            if (tabName === 'sub-spiritcollect') {
                refreshSpiritPanel();
            }
        }

        // 调试信息接收函数
        window.addBattleData = function(type, data) {
            if (type === 'debug_alert') {
                if (typeof updateHelperInfo === 'function') {
                    updateHelperInfo(data);
                }
            }
        };

        // 辅助提示功能
        function updateHelperInfo(text) {
            const helperText = document.getElementById('helper-text');
            if (helperText) {
                // 添加滑动特效
        )HTML" \
R"HTML(        helperText.classList.remove('slide-in');
                // 触发重排
                void helperText.offsetWidth;
                // 更新文本
                helperText.textContent = text;
                // 添加滑动特效类
                helperText.classList.add('slide-in');
            }
        }

        function resetAnimation(element) {
            // 重置动画效果
            element.style.animation = 'none';
            element.offsetHeight; // 强制重排
            element.style.animation = '';
        }
        
        function reanimateTabContent(tabContent) {
            // 重新触发当前标签页内所有元素的动画
            var elements = tabContent.querySelectorAll('.section-title, .control-group, .input, .btn, .list-view, .list-item, .form-row, .divider, .panel-animate');
            
            // 设置递增的延迟时间
            var delay = 0.02;
            for (var i = 0; i < elements.length; i++) {
                var element = elements[i];
                resetAnimation(element);
                
                /)HTML" \
R"HTML(/ 根据元素类型设置不同的动画延迟
                if (element.classList.contains('section-title')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.02;
                } else if (element.classList.contains('control-group') || element.classList.contains('form-row')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.02;
                } else if (element.classList.contains('input') || element.classList.contains('btn')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.01;
                } else if (element.classList.contains('list-view')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.02;
                } else if (element.classList.contains('list-item')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.01;
                } else if (element.classList.contains('divide)HTML" \
R"HTML(r')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.01;
                } else if (element.classList.contains('panel-animate')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.015;
                }
            }
        }
        
        // 绑定基础功能按钮事件
        var refreshGameBtn = document.getElementById('refresh-game');
        var refreshNoLoginBtn = document.getElementById('refresh-no-login');
        var muteGameBtn = document.getElementById('mute-game');
        var clearIECacheBtn = document.getElementById('clear-ie-cache');
        var copyKeyBtn = document.getElementById('copy-key');
        var keyLoginBtn = document.getElementById('key-login');
        var clearIECacheResetTimer = null;

        // 刷新游戏按钮
        if (refreshGameBtn) {
            refreshGameBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                 )HTML" \
R"HTML(   window.chrome.webview.postMessage('refresh-game');
                }
            });
        }

        // 复制 Key 按钮
        if (copyKeyBtn) {
            copyKeyBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({ type: 'copy-login-key' });
                }
            });
        }

        // Key 登录按钮
        if (keyLoginBtn) {
            keyLoginBtn.addEventListener('click', function() {
                showKeyLoginDialog();
            });
        }

        // 不换号刷新按钮
        if (refreshNoLoginBtn) {
            refreshNoLoginBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('refresh-no-login');
                }
            });
        }

        // 游戏静音按钮
        if (muteGameBtn) {
            muteGameBtn.addEventListener('click', function() {
                )HTML" \
R"HTML(if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('mute-game');
                }
            });
        }

        // 清理 IE 缓存按钮
        if (clearIECacheBtn) {
            clearIECacheBtn.addEventListener('click', function() {
                var clearCacheText = document.getElementById('clear-cache-text');

                if (!clearCacheText || !window.chrome || !window.chrome.webview) {
                    return;
                }

                if (clearIECacheResetTimer) {
                    clearTimeout(clearIECacheResetTimer);
                    clearIECacheResetTimer = null;
                }

                clearIECacheBtn.disabled = true;
                clearIECacheBtn.classList.remove('cache-cleared', 'cache-clear-failed');
                clearIECacheBtn.classList.add('cache-clearing');
                clearCacheText.textContent = '清理中';
                window.chrome.webview.postMessage('clear-ie-cache');
          )HTML" \
R"HTML(  });
        }
        
        // 更新静音按钮状态的函数
        window.updateMuteButtonState = function(isMuted) {
            var muteGameBtn = document.getElementById('mute-game');
            var muteText = document.getElementById('mute-text');
            
            if (muteGameBtn && muteText) {
                if (isMuted) {
                    muteGameBtn.classList.add('muted');
                    muteText.textContent = '恢复声音';
                } else {
                    muteGameBtn.classList.remove('muted');
                    muteText.textContent = '游戏静音';
                }
            }
        };

        window.onClearIECacheFinished = function(success) {
            var clearIECacheBtn = document.getElementById('clear-ie-cache');
            var clearCacheText = document.getElementById('clear-cache-text');

            if (!clearIECacheBtn || !clearCacheText) {
                return;
            }

            clearIECacheBtn.disabled = false;
            clearIECacheBtn.cla)HTML" \
R"HTML(ssList.remove('cache-clearing');
            clearIECacheBtn.classList.toggle('cache-cleared', !!success);
            clearIECacheBtn.classList.toggle('cache-clear-failed', !success);
            clearCacheText.textContent = success ? '已清理' : '清理失败';

            if (clearIECacheResetTimer) {
                clearTimeout(clearIECacheResetTimer);
            }

            clearIECacheResetTimer = setTimeout(function() {
                clearIECacheBtn.classList.remove('cache-cleared', 'cache-clear-failed');
                clearCacheText.textContent = '清理缓存';
            }, 1800);
        };
        
        // 定义全局函数，确保 C++ 端可以随时调用
        window.addPacketToList = function(index, direction, hex, time, label) {
            var pListItems = document.getElementById('packet-list-items');
            if (pListItems) {
                var packetItem = document.createElement('div');
                packetItem.className = 'packet-item';
                packetItem.setAttribute('data-hex', hex)HTML" \
R"HTML();
                packetItem.setAttribute('data-index', index - 1);  // 修改：使用从0开始的数组索引

                // 确保direction是简短的形式
                var displayDirection = (direction === '发送包' || direction === '发送') ? '发送' : '接收';
                var isSend = displayDirection === '发送';

                packetItem.innerHTML = `
                    <div style="padding: 6px 8px; border-bottom: 1px solid var(--color-border-hover); cursor: pointer; transition: background 0.15s ease;">
                        
                        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px;">
                            <div style="width: 25px; flex-shrink: 0; font-weight: 600; color: var(--color-text-secondary); font-size: 9px; text-align: center;">
                                ${index}.
                            </div>
                            <div style="width: 30px; flex-shrink: 0; font-weight: 600; color: ${isSend ? '#0078d4' : '#2e7d32'}; font-size: 9px; background)HTML" \
R"HTML(: ${isSend ? 'rgba(0, 120, 212, 0.08)' : 'rgba(46, 125, 50, 0.08)'}; padding: 2px 4px; border-radius: 2px; text-align: center;">
                                ${displayDirection}
                            </div>
                            <div style="width: 50px; flex-shrink: 0; color: var(--color-text-secondary); font-size: 8px; font-family: 'Consolas', monospace;">
                                ${time}
                            </div>
                            <div style="flex: 1; min-width: 0; color: var(--color-primary); font-size: 8px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">
                                ${label || '-'}
                            </div>
                        </div>
                        
                        <div style="color: var(--color-text-primary); font-family: 'Consolas', monospace; font-size: 8px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding-left: 4px;">
            )HTML" \
R"HTML(                ${hex}
                        </div>
                    </div>
                `;

                // 添加点击事件
                packetItem.addEventListener('click', function() {
                    // 移除其他选中状态
                    var allItems = pListItems.querySelectorAll('.packet-item');
                    allItems.forEach(function(item) {
                        item.style.background = '';
                        item.style.borderLeft = '';
                    });
                    // 设置当前选中状态
                    packetItem.style.background = 'rgba(0, 120, 212, 0.08)';
                    packetItem.style.borderLeft = '2.1px solid #0078d4';
                    // 填充到输入框
                    var hexInput = document.getElementById('packet-hex-input');
                    if (hexInput) {
                        hexInput.value = hex;
                    }
                });

                pListItems.appendChild(packetItem);
                pListItems.scrollTop = pList)HTML" \
R"HTML(Items.scrollHeight; // 自动滚动到底底
            }
        };
        window.addPacketToUI = window.addPacketToList; // 兼容性别名

        // 灵玉列表更新功能
        window.currentLingyuData = null;
        window.selectedLingyuIndices = new Set();
        window.currentLingyuData = { items: [] };

        window.updateLingyuUI = function(jsonStr) {
            try {
                const data = typeof jsonStr === 'string' ? JSON.parse(jsonStr) : jsonStr;
                window.currentLingyuData = data;
                // 刷新数据时，保留已选中的索引，但要过滤掉已经不存在的索引
                if (data.items) {
                    const currentIndices = new Set(data.items.map(item => String(item.symmIndex)));
                    for (let idx of window.selectedLingyuIndices) {
                        if (!currentIndices.has(idx)) {
                            window.selectedLingyuIndices.delete(idx);
                        }
                    }
                }
                window.renderLingyuList(data.items);
             )HTML" \
R"HTML(   window.updateSelectedLingyuDisplay();
            } catch (e) {
            }
        };

        window.renderLingyuList = function(items) {
            const listContainer = document.getElementById('lingyu-list');
            const countElement = document.getElementById('lingyu-count');
            
            if (!listContainer) return;
            
            listContainer.innerHTML = '';
            if (!items || items.length === 0) {
                listContainer.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--color-text-tertiary); font-size: 10.5px;">暂无灵玉数据</div>';
                if (countElement) countElement.textContent = '0 个灵玉';
                return;
            }
            
            if (countElement) countElement.textContent = items.length + ' 个灵玉';
            
            items.forEach(item => {
                const isSelected = window.selectedLingyuIndices.has(String(item.symmIndex));
                const row = document.createEleme)HTML" \
R"HTML(nt('div');
                row.style.display = 'grid';
                row.style.gridTemplateColumns = '55px 40px 65px 1fr';
                row.style.padding = '3px 6px';
                row.style.borderBottom = '1px solid var(--color-border)';
                row.style.alignItems = 'center';
                row.style.minHeight = '24px';
                row.style.transition = 'background-color 0.1s linear';
                row.style.cursor = 'pointer';
                row.style.backgroundColor = isSelected ? 'var(--color-background-secondary)' : 'transparent';
                
                row.onclick = () => window.selectLingyu(item);
                row.onmouseover = () => {
                    if (!window.selectedLingyuIndices.has(String(item.symmIndex))) {
                        row.style.backgroundColor = 'var(--color-background-hover)';
                    }
                };
                row.onmouseout = () => {
                    if (!window.selectedLingyuIndices.has()HTML" \
R"HTML(String(item.symmIndex))) {
                        row.style.backgroundColor = 'transparent';
                    } else {
                        row.style.backgroundColor = 'var(--color-background-secondary)';
                    }
                };
                
                const petName = item.petName || '无';
                const petColor = item.petName ? 'var(--color-primary)' : 'var(--color-text-tertiary)';
                const symmName = item.symmName || '未知';
                
                let attrHtml = '';
                if (item.nativeList && item.nativeList.length > 0) {
                    item.nativeList.forEach(attr => {
                        attrHtml += `<span style="display: inline-block; background: #e8f5e8; color: #2e7d32; padding: 1px 3px; border-radius: 2px; margin-right: 2px; font-weight: 500; font-size: 9.5px;">${attr.nativeName}:${attr.nativeValue}</span>`;
                    });
                } else {
                    attrHtml = '<span styl)HTML" \
R"HTML(e="color: var(--color-text-tertiary); font-size: 10.5px;">无</span>';
                }
                
                row.innerHTML = `
                    <div style="text-align: center; color: var(--color-text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 10.5px;">${symmName}</div>
                    <div style="text-align: center; color: var(--color-text-secondary); font-size: 10.5px;">${item.symmIndex}</div>
                    <div style="text-align: center; color: ${petColor}; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 10.5px;">${petName}</div>
                    <div style="display: flex; flex-wrap: wrap; gap: 2px; font-size: 9.5px;">${attrHtml}</div>
                `;
                
                listContainer.appendChild(row);
            });
        };

        window.selectLingyu = function(item) {
            const indexStr = String(item.symmIndex);
            if (window.selectedLingyuIndices.h)HTML" \
R"HTML(as(indexStr)) {
                window.selectedLingyuIndices.delete(indexStr);
            } else {
                window.selectedLingyuIndices.add(indexStr);
                // 自动将选中的灵玉添加到分解队列
                addToDecomposeQueueFromSelection(item);
                // 添加后自动取消选中状态
                setTimeout(() => {
                    window.selectedLingyuIndices.delete(indexStr);
                    // 重新渲染列表以更新选中状态背景色
                    window.renderLingyuList(window.currentLingyuData.items);
                    // 更新右侧显示
                    window.updateSelectedLingyuDisplay();
                }, 300); // 延迟300ms取消选中，让用户能看到添加动作
            }
            
            // 重新渲染列表以更新选中状态背景色
            window.renderLingyuList(window.currentLingyuData.items);
            // 更新右侧显示
            window.updateSelectedLingyuDisplay();
        };
        
        // 从选择直接添加到分解队列的函数
        window.addToDecomposeQueueFromSelection = function(item) {
            const indexStr = String(item.symmI)HTML" \
R"HTML(ndex);

            // 检查是否已存在于分解队列中
            const exists = window.decomposeQueue.some(q => q.index === indexStr);
            if (!exists) {
                window.decomposeQueue.push({
                    index: indexStr,
                    name: item.symmName || '未知',
                    level: item.symmLevel || 'N',
                    attrs: item.nativeList ? item.nativeList.map(attr => `${attr.nativeName}:${attr.nativeValue}`).join(',') : ''
                });

                // 重新渲染分解队列
                window.renderDecomposeQueue();
            }
        };

        window.updateSelectedLingyuDisplay = function() {
            const infoDiv = document.getElementById('selected-lingyu-info');
            if (!infoDiv) return;
            
            if (window.selectedLingyuIndices.size === 0) {
                infoDiv.innerHTML = '请从左侧列表选择灵玉';
                return;
            }
            
            let html = `<div style="font-weight: 600; color: var(--color-primar)HTML" \
R"HTML(y); margin-bottom: 4px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">已选择 (${window.selectedLingyuIndices.size})</div>`;
            
            // 找到对应的灵玉对象并显示
            const selectedItems = (window.currentLingyuData.items || []).filter(item => 
                window.selectedLingyuIndices.has(String(item.symmIndex))
            );
            
            selectedItems.forEach(item => {
                let attrs = item.nativeList.map(a => `${a.nativeName}:${a.nativeValue}`).join(',');
                html += `
                    <div style="margin-bottom: 4px; padding: 2px; border-radius: 2px; background: rgba(0,0,0,0.02); border: 1px solid rgba(0,0,0,0.05);">
                        <div style="display: flex; justify-content: space-between;">
                            <span style="font-weight: 500;">${item.symmName}</span>
                            <span style="color: var(--color-text-tertiary);">#${item.symmIndex}</span>
                        </div>)HTML" \
R"HTML(
                        <div style="font-size: 8.5px; color: var(--color-text-secondary);">${attrs || '无属性'}</div>
                    </div>
                `;
            });
            
            infoDiv.innerHTML = html;
        };

        window.queryLingyu = function() {
            // 调用C++端的灵玉查询函数
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'query_lingyu'
                });
            }
        };

        // 分解队列功能
        window.decomposeQueue = []; // 存储要分解的灵玉对象 {index, name}
        window.selectedLingyuIndices = new Set(); // 左侧列表选中项
        window.isDecomposing = false; // 是否正在分解



        window.clearDecomposeQueue = function() {
            if (!window.decomposeQueue || window.decomposeQueue.length === 0) {
                return;
            }

            // 不使用confirm，直接清空队列
            window.decomposeQueue = [];
            window.renderDecomposeQueue();
        };

)HTML" \
R"HTML(        // 从分解队列中移除单个项目
        window.removeItemFromDecomposeQueue = function(index) {
            // 从队列中移除指定索引的项目
            window.decomposeQueue = window.decomposeQueue.filter(q => q.index !== index);

            // 重新渲染队列
            window.renderDecomposeQueue();
        };

        window.renderDecomposeQueue = function() {
            try {
                var listContainer = document.getElementById('decompose-list');
                var countElement = document.getElementById('decompose-queue-count');
                
                if (!listContainer) return;
                
                if (!window.decomposeQueue) {
                    window.decomposeQueue = [];
                }
                
                if (window.decomposeQueue.length === 0) {
                    listContainer.innerHTML = '<div style="text-align:center;padding:10px;color:var(--color-text-tertiary);font-size:10.5px;">未添加灵玉</div>';
                    if (countElement) countElement.textConten)HTML" \
R"HTML(t = '已添加: 0 个灵玉';
                    return;
                }
                
                if (countElement) countElement.textContent = '已添加: ' + window.decomposeQueue.length + ' 个灵玉';
                
                // 使用flexbox布局，一行显示两个项目
                var html = '<div style="display:flex;flex-wrap:wrap;gap:3px;">';
                for (var i = 0; i < window.decomposeQueue.length; i++) {
                    var item = window.decomposeQueue[i];
                    // 每个项目包含选择框、序号、名称、属性和删除按钮
                    html += '<div style="flex:1;min-width:130px;max-width:220px;display:flex;align-items:center;padding:3px 5px;border:1px solid var(--color-border);border-radius:3px;align-items:center;gap:4px;font-size:9.5px;">';
                    // 选择框
                    html += '<input type="checkbox" class="queue-item-checkbox" data-index="' + item.index + '" style="width:12px;height:12px;flex-shrink:0;" />';
                    // 序号
                    html += '<span style="displ)HTML" \
R"HTML(ay:inline-block;background:var(--color-background-secondary);color:var(--color-text-primary);padding:1px 4px;border-radius:2px;font-size:9.5px;font-weight:500;white-space:nowrap;flex-shrink:0;">#' + item.index + '</span>';
                    // 名称
                    html += '<span style="color:var(--color-text-primary);font-size:10.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1;min-width:0;">' + (item.name || '未知') + '</span>';
                    // 属性（简约显示）
                    if (item.attrs && item.attrs.length > 0) {
                        html += '<span style="display:inline-block;background:#e8f5e8;color:#2e7d32;padding:1px 3px;border-radius:2px;font-weight:500;font-size:8.5px;flex-shrink:0;">' + item.attrs + '</span>';
                    }
                    // 美化的删除按钮
                    html += '<button onclick="window.removeItemFromDecomposeQueue(\'' + item.index + '\')" style="width:16px;height:16px;border:none;border-radius:50%;background:var(--co)HTML" \
R"HTML(lor-text-tertiary);color:white;font-size:9.5px;cursor:pointer;display:flex;align-items:center;justify-content:center;font-weight:bold;">×</button>';
                    html += '</div>';
                }
                html += '</div>';
                listContainer.innerHTML = html;
            } catch (e) {
            }
        };

        window.decomposeAllInQueue = function() {
            try {
                // 初始化队列
                if (!window.decomposeQueue) {
                    window.decomposeQueue = [];
                }

                // 检查是否正在分解
                if (window.isDecomposing) {
                    // 更新辅助提示文本
                    if (window.updateHelperText) {
                        window.updateHelperText('正在分解中，请稍候...');
                    }
                    return;
                }

                // 检查队列是否为空
                if (window.decomposeQueue.length === 0) {
                    // 更新辅助提示文本
                    if (window.updateHelperText))HTML" \
R"HTML( {
                        window.updateHelperText('分解队列为空，请先添加灵玉');
                    }
                    return;
                }

                var count = window.decomposeQueue.length;

                // 直接继续执行，不使用confirm对话框，因为可能导致卡死

                // 标记为正在分解
                window.isDecomposing = true;

                // 收集索引
                var indices = [];
                for (var i = 0; i < window.decomposeQueue.length; i++) {
                    indices.push(window.decomposeQueue[i].index);
                }

                // 发送消息
                if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                    // 构建分解请求
                    const request = {
                        type: 'decompose_lingyu_batch',
                        indices: indices
                    };

                    // 模拟构建分解封包的十六进制表示
                    const hexPacket = buildDecomposePacketHex(indices);

                    window.chrome.webvie)HTML" \
R"HTML(w.postMessage(request);
                } else {
                    // 更新辅助提示文本
                    if (window.updateHelperText) {
                        window.updateHelperText('错误: WebView2 API不可用');
                    }
                    window.isDecomposing = false;
                }
            } catch (e) {
                window.isDecomposing = false;
                // 更新辅助提示文本
                if (window.updateHelperText) {
                    window.updateHelperText('分解过程中出现错误: ' + e.message);
                }
            }
        };
        
        // 模拟构建分解封包的十六进制表示
        window.buildDecomposePacketHex = function(indices) {
            try {
                // 将索引数组转换为字符串，例如 ["62", "61"] -> "62_61"
                const indicesStr = indices.join('_');

                // 解析第一个索引并转换为整数（用于模拟）
                const firstIndex = parseInt(indices[0]); // 使用第一个索引作为示例

                // 构建十六进制表示 (按照正确的协议格式 - 使用整数格式)
                let hex = "";

                // 1. Ma)HTML" \
R"HTML(gic: "SD" (2字节)
                hex += "44 53 ";

                // 2. Length field (2字节，固定为00 00)
                hex += "00 00 ";

                // 3. Opcode: 0x00121816 (4字节)
                hex += "16 18 12 00 ";

                // 4. Index as 8-byte integer in little endian format
                // 例如，如果索引是500 (0x01F4):
                // 0xF4 0x01 0x00 0x00 0x00 0x00 0x00 0x00
                for (let i = 0; i < 8; i++) {
                    hex += ((firstIndex >> (i * 8)) & 0xFF).toString(16).padStart(2, '0').toUpperCase();
                    if (i < 7) hex += " ";
                }
                hex += " ";

                // 5. Unknown bytes: 00 00 (2字节)
                hex += "00 00";

                return hex.trim();
            } catch (e) {
                        return "构建失败";
                    }        };

        // 处理C++端返回的分解完成消息
        window.onDecomposeComplete = function() {
            // 清空队列
            window.decomposeQueue = [];
            wind)HTML" \
R"HTML(ow.renderDecomposeQueue();

            // 清空左侧选中
            window.selectedLingyuIndices.clear();

            // 安全地重新渲染灵玉列表
            if (window.currentLingyuData && window.currentLingyuData.items) {
                window.renderLingyuList(window.currentLingyuData.items);
            } else {
                window.renderLingyuList([]);
            }

            window.isDecomposing = false;

            // 发送查询请求刷新列表
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({type: 'query_lingyu'});
            }
        };

        // ============== 日常活动功能 ==============
        window.isDailyTaskRunning = false;
        window.isTaskZoneRunning = false;


        // 更新日常活动状态显示
        window.updateDailyTaskStatus = function(text, isRunning) {
            var statusEl = document.getElementById('daily-task-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = isR)HTML" \
R"HTML(unning ? 'var(--color-primary)' : 'var(--color-text-secondary)';
            }
            window.isDailyTaskRunning = isRunning;
            
            // 更新所有相关按钮状态
            var dailyBtn = document.getElementById('complete-daily-tasks');
            var trialBtn = document.getElementById('complete-trial-tasks');
            var danceBtn = document.getElementById('complete-dance-tasks');
            
            if (dailyBtn) {
                dailyBtn.disabled = isRunning;
                dailyBtn.textContent = isRunning ? '执行中...' : '一键完成';
            }
            if (trialBtn) {
                trialBtn.disabled = isRunning;
                trialBtn.textContent = isRunning ? '执行中...' : '一键试炼';
            }
            if (danceBtn) {
                danceBtn.disabled = isRunning;
                danceBtn.textContent = isRunning ? '执行中...' : '一键跳舞';
            }
            var activityBtn = document.getElementById('complete-activity-tasks');
            if (activityBtn) {
)HTML" \
R"HTML(                activityBtn.disabled = isRunning;
                activityBtn.textContent = isRunning ? '执行中...' : '一键活动';
            }
        };

        // 一键完成日常活动
        window.completeDailyTasks = function() {
            if (window.isDailyTaskRunning) return;
            
            // 收集选中的任务（新增活动）- 只处理每日模块
            var flags = 0;
            if (document.getElementById('daily-deep-dig').checked) flags |= 0x01;   // 深度挖宝
            if (document.getElementById('daily-card').checked) flags |= 0x02;       // 每日卡牌
            if (document.getElementById('daily-gift').checked) flags |= 0x04;       // 每日礼包
            if (document.getElementById('weekly-gift').checked) flags |= 0x08;      // 每周礼包
            if (document.getElementById('family-checkin').checked) flags |= 0x10;   // 家族考勤
            if (document.getElementById('family-report').checked) flags |= 0x20;    // 家族报道
            if (document.getElementById('family-defend').checked) flags |= 0x40;    // 家族保卫
         )HTML" \
R"HTML(   if (document.getElementById('shop-surprise').checked) flags |= 0x80;    // 商城惊喜
            
            if (flags === 0) {
                window.updateDailyTaskStatus('请至少勾选一项日常活动', false);
                window.updateHelperText('请至少勾选一项日常活动');
                return;
            }
            
            window.updateDailyTaskStatus('正在执行日常活动，请勿操作...', true);
            window.updateHelperText('正在执行日常活动，请勿操作...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'daily_tasks',
                    flags: flags
                });
            }
        };

        // 一键活动：每个活动各自跑在独立的受管线程里，彼此不知道对方在做什么，
        // 所以这里串行下发。间隔按各自真实耗时给足——金平府 3 轮是
        // 3*(hold_ms + 1200) 秒，用固定 10 秒会让下一项在它还没跑完时就启动。
        window.completeActivityTasks = function() {
            if (window.isDailyTaskRunning) return;

            var steps = [];

            var runCompEl = d)HTML" \
R"HTML(ocument.getElementById('act-run-competition');
            if (runCompEl && runCompEl.checked) {
                steps.push({
                    label: '障碍赛跑',
                    msg: { type: 'daily_run_competition', times: 3 },
                    waitMs: 4000
                });
            }
            var jinpingfuEl = document.getElementById('act-jinpingfu-wish');
            if (jinpingfuEl && jinpingfuEl.checked) {
                steps.push({
                    label: '金平府祈愿',
                    msg: { type: 'daily_jinpingfu_wish', times: 3, hold_ms: 300 },
                    waitMs: 7000
                });
            }
            var knowledgeEl = document.getElementById('act-knowledge-garden');
            if (knowledgeEl && knowledgeEl.checked) {
                steps.push({
                    label: '知识小天地',
                    msg: { type: 'daily_knowledge_garden', times: 1, hold_ms: 300 },
                    waitMs: 3000
                });
            }
      )HTML" \
R"HTML(      var gomokuEl = document.getElementById('act-gomoku');
            if (gomokuEl && gomokuEl.checked) {
                steps.push({
                    label: '五子棋',
                    msg: { type: 'daily_gomoku', times: 1 },
                    waitMs: 12000
                });
            }

            if (steps.length === 0) {
                window.updateDailyTaskStatus('请至少勾选一项活动', false);
                window.updateHelperText('请至少勾选一项活动');
                return;
            }

            var total = steps.length;
            var index = 0;

            function runNextStep() {
                if (index >= total) {
                    window.updateDailyTaskStatus('活动已全部执行完成（' + total + ' 项）', false);
                    return;
                }
                var step = steps[index];
                window.updateDailyTaskStatus('正在执行 ' + step.label + '（' + (index + 1) + '/' + total + '）...', true);
                if (window.chrome && window.chrome.webview) {
        )HTML" \
R"HTML(            window.chrome.webview.postMessage(step.msg);
                }
                var delay = step.waitMs || 10000;
                index++;
                setTimeout(runNextStep, delay);
            }

            runNextStep();
        };

        // 一键完成试炼活动
        window.completeTrialTasks = function() {
            if (window.isDailyTaskRunning) return;
            
            // 只收集试炼相关的任务
            var flags = 0;
            var trialNames = [];
            if (document.getElementById('daily-fire-wind').checked) {
                flags |= 0x200;
                trialNames.push('火风试炼');
            }
            if (document.getElementById('daily-fire').checked) {
                flags |= 0x400;
                trialNames.push('火焰试炼');
            }
            if (document.getElementById('daily-storm').checked) {
                flags |= 0x800;
                trialNames.push('风暴试炼');
            }
            
            if (flags === 0) {
                window.)HTML" \
R"HTML(updateDailyTaskStatus('请至少勾选一项试炼活动', false);
                window.updateHelperText('请至少勾选一项试炼活动');
                return;
            }
            
            window.updateDailyTaskStatus('正在执行 ' + trialNames.join('、') + '...', true);
            window.updateHelperText('正在执行 ' + trialNames.join('、') + '...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'daily_tasks',
                    flags: flags
                });
            }
        };

        // 玄塔功能 - 一键跑塔（易语言风格，盲投20次）
        window.oneKeyXuantta = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'one_key_xuantta'
                }));
            }
        };

        // 玄塔功能 - 购买18个骰子
        window.buyDice18 = function() {
            if (window.chrome && window.chrome.webvi)HTML" \
R"HTML(ew) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'buy_dice_18'
                }));
            }
        };

        // 双台谷刷级 - 查询妖怪数据
        window.queryShuangTai = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'query_shuangtai'
                }));
            }
        };

        // 双台谷刷级 - 一键启动
        window.startShuangTai = function() {
            var blockBattleCb = document.getElementById('shuangtai-block-battle');
            var blockBattle = blockBattleCb ? blockBattleCb.checked : false;
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'start_shuangtai',
                    blockBattle: blockBattle
                }));
                // 更新按钮状态
                var startBtn = document.getElementByI)HTML" \
R"HTML(d('start-shuangtai');
                var stopBtn = document.getElementById('stop-shuangtai');
                if (startBtn) startBtn.disabled = true;
                if (stopBtn) stopBtn.disabled = false;
            }
        };

        // 双台谷刷级 - 停止
        window.stopShuangTai = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'stop_shuangtai'
                }));
                // 更新按钮状态
                var startBtn = document.getElementById('start-shuangtai');
                var stopBtn = document.getElementById('stop-shuangtai');
                if (startBtn) startBtn.disabled = false;
                if (stopBtn) stopBtn.disabled = true;
            }
        };
        
        // 双台谷刷级 - 更新妖怪和技能下拉框
        window.updateShuangTaiUI = function(petName, skillName, petCount, canStart) {
            var petSelect = document.getElementById('shuangtai-pet');
     )HTML" \
R"HTML(       var skillSelect = document.getElementById('shuangtai-skill');
            var startBtn = document.getElementById('start-shuangtai');
            var stopBtn = document.getElementById('stop-shuangtai');
            
            if (petSelect) {
                petSelect.innerHTML = '<option value="">' + petName + '</option>';
                petSelect.disabled = false;
            }
            if (skillSelect) {
                skillSelect.innerHTML = '<option value="">' + skillName + '</option>';
                skillSelect.disabled = false;
            }
            if (startBtn) {
                startBtn.disabled = !canStart;
            }
            if (stopBtn) {
                stopBtn.disabled = true;  // 初始状态停止按钮禁用
            }
        };

        // 主线任务清单由 native 侧从 taskinfo/taskprop 配方推送（PushStoryTaskDefinitionToUi）。
        // 这里只保留一个空占位，避免 native 尚未推送时面板报错。
        window.huangchengWeijiTaskDefs = [];

        window.setHuangchengWeijiTaskDefs = function(defs) {
)HTML" \
R"HTML(            if (!defs || !defs.length) {
                return;
            }
            window.huangchengWeijiTaskDefs = defs;
            renderHuangchengWeijiTasks();
        };

        window.huangchengWeijiState = {
            hideCompleted: true,
            isRunning: false,
            activeGroup: '',
            selectedMap: {},
            acceptedMap: {},
            completedMap: {}
        };

        function countMapValues(map) {
            var count = 0;
            for (var key in map) {
                if (Object.prototype.hasOwnProperty.call(map, key) && map[key]) {
                    count++;
                }
            }
            return count;
        }

        function escapeHtml(text) {
            return String(text)
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/"/g, '&quot;')
                .replace(/'/g, '&#39;');
        }

        // 配方按子任务顺序推送，同一 )HTML" \
R"HTML(taskName 即一个任务系列（皇城危机 / 魔晶战记 …）。
        function collectHuangchengWeijiGroups() {
            var groups = [];
            var byName = {};
            for (var i = 0; i < window.huangchengWeijiTaskDefs.length; i++) {
                var task = window.huangchengWeijiTaskDefs[i];
                var name = task.taskName || '未分类';
                if (!byName[name]) {
                    byName[name] = { name: name, taskId: task.taskId, items: [] };
                    groups.push(byName[name]);
                }
                byName[name].items.push(task);
            }
            return groups;
        }

        function countGroupDone(group) {
            var state = window.huangchengWeijiState;
            var done = 0;
            for (var i = 0; i < group.items.length; i++) {
                if (state.completedMap[String(group.items[i].subtaskId)]) {
                    done++;
                }
            }
            return done;
        }

        // 当前系列：优先用用户点选的；否则落在第一个还)HTML" \
R"HTML(有未完成子任务的系列。
        function pickActiveHuangchengGroup(groups) {
            var state = window.huangchengWeijiState;
            for (var i = 0; i < groups.length; i++) {
                if (groups[i].name === state.activeGroup) {
                    return groups[i];
                }
            }
            for (var j = 0; j < groups.length; j++) {
                if (countGroupDone(groups[j]) < groups[j].items.length) {
                    return groups[j];
                }
            }
            return groups.length ? groups[0] : null;
        }

        function renderHuangchengWeijiGroups(groups, active) {
            var groupEl = document.getElementById('huangcheng-weiji-groups');
            if (!groupEl) {
                return;
            }
            var html = '';
            for (var i = 0; i < groups.length; i++) {
                var group = groups[i];
                html += '<button class="hw-group' + (group === active ? ' active' : '') +
                   )HTML" \
R"HTML(     '" onclick="window.selectHuangchengWeijiGroup(' + i + ')">' +
                        escapeHtml(group.name) +
                        '<span class="hw-group-count">' + countGroupDone(group) + '/' + group.items.length + '</span>' +
                        '</button>';
            }
            groupEl.innerHTML = html;
        }

        function renderHuangchengWeijiTasks() {
            var groups = collectHuangchengWeijiGroups();
            window.huangchengWeijiGroups = groups;
            var state = window.huangchengWeijiState;
            var hideCompleted = state.hideCompleted !== false;
            var active = pickActiveHuangchengGroup(groups);
            if (active) {
                state.activeGroup = active.name;
            }
            renderHuangchengWeijiGroups(groups, active);

            var taskEl = document.getElementById('huangcheng-weiji-tasks');
            var summaryEl = document.getElementById('huangcheng-weiji-summary');
            if (!taskEl) {
)HTML" \
R"HTML(                return;
            }

            var total = window.huangchengWeijiTaskDefs.length;
            var completedCount = 0;
            var acceptedCount = 0;
            var selectedCount = 0;
            for (var i = 0; i < total; i++) {
                var key = String(window.huangchengWeijiTaskDefs[i].subtaskId);
                if (state.completedMap[key]) completedCount++;
                if (state.acceptedMap[key]) acceptedCount++;
                if (state.selectedMap[key]) selectedCount++;
            }

            var html = '';
            if (active) {
                for (var t = 0; t < active.items.length; t++) {
                    var task = active.items[t];
                    var taskKey = String(task.subtaskId);
                    var isCompleted = !!state.completedMap[taskKey];
                    var isAccepted = !!state.acceptedMap[taskKey];
                    var isSelected = !!state.selectedMap[taskKey];
                    if (hideCompleted && )HTML" \
R"HTML(isCompleted) {
                        continue;
                    }
                    var statusText = isCompleted ? '已完成' : (isAccepted ? '进行中' : '待执行');
                    var tip = task.subtaskId + ' · ' + (task.sceneName || '') + ' · ' + statusText;
                    html += '<label class="hw-task' + (isCompleted ? ' hw-done' : '') +
                            (isAccepted ? ' hw-accepted' : '') + '" title="' + escapeHtml(tip) + '">' +
                            '<input type="checkbox" ' + (isSelected ? 'checked' : '') +
                            (isCompleted ? ' disabled' : '') +
                            ' onchange="window.setHuangchengWeijiSelection(' + task.subtaskId + ', this.checked)">' +
                            '<span>' + escapeHtml(task.name || taskKey) + '</span>' +
                            '</label>';
                }
            }
            if (!html) {
                html = '<div style="font-size: 9.5px; color: var(--color-text-tertiary); padding)HTML" \
R"HTML(: 4px;">' +
                       (active ? '该系列已全部完成' : '正在加载主线任务列表...') + '</div>';
            }
            taskEl.innerHTML = html;

            if (summaryEl) {
                summaryEl.textContent = '已完成: ' + completedCount + ' / ' + total +
                    '，已接: ' + acceptedCount + '，已勾选: ' + selectedCount;
            }
            updateHuangchengWeijiButtons();
        }

        function updateHuangchengWeijiButtons() {
            var state = window.huangchengWeijiState;
            var startBtn = document.getElementById('start-huangcheng-weiji');
            if (startBtn) {
                startBtn.disabled = state.isRunning || countMapValues(state.selectedMap) === 0;
                startBtn.textContent = state.isRunning ? '执行中...' : '完成所选';
            }
            var stopBtn = document.getElementById('stop-huangcheng-weiji');
            if (stopBtn) {
                stopBtn.disabled = !state.isRunning;
            }
        }

        window.selectHuangchengW)HTML" \
R"HTML(eijiGroup = function(index) {
            var groups = window.huangchengWeijiGroups || [];
            if (index < 0 || index >= groups.length) {
                return;
            }
            window.huangchengWeijiState.activeGroup = groups[index].name;
            renderHuangchengWeijiTasks();
        };

        window.setHuangchengWeijiSelection = function(subtaskId, checked) {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
            var key = String(subtaskId);
            if (checked) {
                state.selectedMap[key] = true;
            } else {
                delete state.selectedMap[key];
            }
            renderHuangchengWeijiTasks();
        };

        window.selectAllHuangchengWeiji = function() {
            var state = window.huangchengWeijiState;
            var groups = window.huangchengWeijiGroups || [];
            var active = null;
            for (var i = 0; i < groups.lengt)HTML" \
R"HTML(h; i++) {
                if (groups[i].name === state.activeGroup) {
                    active = groups[i];
                    break;
                }
            }
            if (!active) {
                return;
            }
            for (var j = 0; j < active.items.length; j++) {
                var key = String(active.items[j].subtaskId);
                if (!state.completedMap[key]) {
                    state.selectedMap[key] = true;
                }
            }
            renderHuangchengWeijiTasks();
        };

        window.clearHuangchengWeijiSelection = function() {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
            state.selectedMap = {};
            renderHuangchengWeijiTasks();
        };

        window.toggleHuangchengWeijiHideCompleted = function(enabled) {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
    )HTML" \
R"HTML(        state.hideCompleted = !!enabled;
            renderHuangchengWeijiTasks();
        };

        // USER_TASK_LIST 一次只回一个任务组，进度按组累加，不覆盖其它系列的结果。
        window.updateHuangchengWeijiProgress = function(payload) {
            var state = window.huangchengWeijiState;
            if (!state || !payload) {
                return;
            }
            var accepted = payload.acceptedSubtaskIds || [];
            var finished = payload.finishedSubtaskIds || [];
            for (var i = 0; i < accepted.length; i++) {
                state.acceptedMap[String(accepted[i])] = true;
            }
            for (var j = 0; j < finished.length; j++) {
                var key = String(finished[j]);
                state.completedMap[key] = true;
                delete state.selectedMap[key];
            }
            renderHuangchengWeijiTasks();
        };

        window.updateHuangchengWeijiStatus = function(text, isRunning) {
            var state = window.huangchengWeijiState;
       )HTML" \
R"HTML(     if (state) {
                state.isRunning = !!isRunning;
            }
            var statusEl = document.getElementById('huangcheng-weiji-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = isRunning ? 'var(--color-primary)' : 'var(--color-text-secondary)';
            }
            updateHuangchengWeijiButtons();
        };

        window.refreshHuangchengWeijiTaskProgress = function() {
            if (!(window.chrome && window.chrome.webview)) {
                return;
            }
            // 按当前系列的 taskId 查询，否则刷新拿不到该组进度。
            var state = window.huangchengWeijiState;
            var groups = (window.huangchengWeijiGroups && window.huangchengWeijiGroups.length)
                ? window.huangchengWeijiGroups
                : collectHuangchengWeijiGroups();
            var taskId = 2001000;
            for (var i = 0; i < groups.length; i++) {
                if (groups[i].name === state.activ)HTML" \
R"HTML(eGroup && groups[i].taskId) {
                    taskId = groups[i].taskId;
                    break;
                }
            }
            window.chrome.webview.postMessage({
                type: 'query_huangcheng_weiji_task_progress',
                task_id: taskId
            });
        };

        window.startHuangchengWeijiTask = function() {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }

            var selectedIds = [];
            for (var key in state.selectedMap) {
                if (Object.prototype.hasOwnProperty.call(state.selectedMap, key) && state.selectedMap[key]) {
                    selectedIds.push(parseInt(key, 10));
                }
            }
            selectedIds.sort(function(a, b) { return a - b; });

            if (selectedIds.length === 0) {
                window.updateHelperText('请先勾选要完成的子任务');
                return;
            }

            window.updateHuangcheng)HTML" \
R"HTML(WeijiStatus('正在同步主线任务进度...', true);
            window.updateHelperText('正在同步主线任务进度...');

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'start_huangcheng_weiji_task',
                    task_id: 2001000,
                    subtask_ids: selectedIds
                });
            }
        };

        window.stopHuangchengWeijiTask = function() {
            window.updateHuangchengWeijiStatus('正在停止主线任务...', true);
            window.updateHelperText('正在停止主线任务...');
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'stop_huangcheng_weiji_task'
                });
            }
        };

        window.updateTaskZoneStatus = function(text, isRunning) {
            var statusEl = document.getElementById('task-zone-status');
            if (statusEl) {
                statusEl.textContent = text;
                )HTML" \
R"HTML(statusEl.style.color = isRunning ? 'var(--color-primary)' : 'var(--color-text-secondary)';
            }
            window.isTaskZoneRunning = isRunning;

            var startBtn = document.getElementById('start-task-zone');
            var stopBtn = document.getElementById('stop-task-zone');
            if (startBtn) {
                startBtn.disabled = isRunning;
                startBtn.textContent = isRunning ? '执行中...' : '开始';
            }
            if (stopBtn) {
                stopBtn.disabled = !isRunning;
                stopBtn.textContent = '停止';
            }
        };

        window.startTaskZone = function() {
            if (window.isTaskZoneRunning) return;

            window.updateTaskZoneStatus('正在执行任务区，请勿操作...', true);
            window.updateHelperText('正在执行任务区，请勿操作...');

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'task_zone'
                });
            }
   )HTML" \
R"HTML(     };

        window.stopTaskZone = function() {
            if (!window.isTaskZoneRunning) return;

            window.updateHelperText('正在停止任务区...');
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'stop_task_zone'
                });
            }
        };

        // ====================================================================
        // 最新活动：勾选式批量执行（与材料采集同一交互模型）
        // 剩余次数由 C++ 侧通过 window.updateActivityCounts 回填；次数为 0
        // 时自动禁用该行，避免无效操作。
        // ====================================================================
        window.actLatestIds = [647, 869, 749, 873, 742, 721, 877, 730, 880];

        // C++ 回填各活动剩余次数：{ "717": 3, "822": 0, ... }
        // 值为负数或 null 表示未知，显示“--”且不禁用。
        window.updateActivityCounts = function(counts) {
            if (!counts) return;
            window.actLatestCounts = window.actLatestCounts || {};
            window.actLatestIds.f)HTML" \
R"HTML(orEach(function(id) {
                var raw = counts[id];
                if (raw === undefined) raw = counts[String(id)];
                if (raw === undefined) return;
                window.actLatestCounts[id] = raw;
                window.applyActivityCount(id, raw);
            });
        };

        window.applyActivityCount = function(id, raw) {
            var countEl = document.getElementById('act-count-' + id);
            var rowEl = document.getElementById('act-row-' + id);
            var pickEl = document.getElementById('act-check-' + id);
            if (!countEl || !rowEl) return;

            var n = (raw === null || raw === undefined) ? null : Number(raw);
            if (n === null || isNaN(n) || n < 0) {
                // 未知：显示占位，保持可勾选
                countEl.textContent = '--';
                countEl.className = 'act-item-count act-count-unknown';
                rowEl.classList.remove('act-disabled');
                if (pickEl) pickEl.disabled = false;
     )HTML" \
R"HTML(           return;
            }

            countEl.textContent = '剩 ' + n;
            if (n <= 0) {
                countEl.className = 'act-item-count act-count-zero';
                rowEl.classList.add('act-disabled');
                if (pickEl) { pickEl.checked = false; pickEl.disabled = true; }
            } else {
                countEl.className = 'act-item-count';
                rowEl.classList.remove('act-disabled');
                if (pickEl) pickEl.disabled = false;
            }
        };

        // 全选 / 全不选（跳过已禁用的行）
        window.selectAllActivities = function(checked) {
            window.actLatestIds.forEach(function(id) {
                var pickEl = document.getElementById('act-check-' + id);
                if (pickEl && !pickEl.disabled) pickEl.checked = checked;
            });
        };

        // 请求刷新各活动剩余次数（C++ 侧逐个查询活动信息后回填）
        window.refreshActivityCounts = function() {
            window.updateHelperText('正在获取活动剩余次数...');
            if (windo)HTML" \
R"HTML(w.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'refresh_activity_counts'
                }));
            }
        };

        // 一键执行所勾选的活动：整批交给 C++ 单线程顺序执行。
        // 每个活动会在 C++ 侧循环到次数耗尽，并在每轮结束自动回填剩余次数，
        // 因此这里只下发一次，不再逐个 setTimeout。
        window.runSelectedActivities = function() {
            var picked = [];
            window.actLatestIds.forEach(function(id) {
                var pickEl = document.getElementById('act-check-' + id);
                if (pickEl && pickEl.checked && !pickEl.disabled) picked.push(id);
            });

            if (picked.length === 0) {
                window.updateHelperText('请至少勾选一个活动');
                return;
            }

            if (!(window.chrome && window.chrome.webview)) return;

            window.updateHelperText('开始执行 ' + picked.length + ' 个活动（顺序执行，直到次数用完）...');
            window.chrome.webview.postMessage(JSON.stringify({
           )HTML" \
R"HTML(     action: 'one_key_latest_batch',
                activity_ids: picked
            }));
        };

        // 停止最新活动一键执行
        window.stopSelectedActivities = function() {
            window.updateHelperText('正在停止最新活动...');
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'stop_latest_activities'
                }));
            }
        };

        window.heavenFuruiRunning = false;

        // 福瑞宝箱 - 开始
        window.startHeavenFurui = function() {
            if (window.heavenFuruiRunning) return;
            
            var maxBoxes = parseInt(document.getElementById('heaven-furui-max').value) || 30;
            if (maxBoxes < 1) maxBoxes = 1;
            if (maxBoxes > 30) maxBoxes = 30;
            document.getElementById('heaven-furui-max').value = maxBoxes;
            document.getElementById('heaven-furui-target').textContent = maxBoxes;
            document.getElem)HTML" \
R"HTML(entById('heaven-furui-count').textContent = '0';
            
            window.heavenFuruiRunning = true;
            
            // 更新按钮状态
            document.getElementById('start-heaven-furui').disabled = true;
            document.getElementById('stop-heaven-furui').disabled = false;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'start_heaven_furui',
                    maxBoxes: maxBoxes
                }));
            }
        };

        // 福瑞宝箱 - 停止
        window.stopHeavenFurui = function() {
            if (!window.heavenFuruiRunning) return;
            
            window.heavenFuruiRunning = false;
            
            // 更新按钮状态
            document.getElementById('start-heaven-furui').disabled = false;
            document.getElementById('stop-heaven-furui').disabled = true;
            
            if (window.chrome && window.chrome.webview))HTML" \
R"HTML( {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'stop_heaven_furui'
                }));
            }
        };

        // 福瑞宝箱 - 更新进度（C++调用）
        window.updateHeavenFuruiProgress = function(current, total) {
            document.getElementById('heaven-furui-count').textContent = current;
            document.getElementById('heaven-furui-target').textContent = total;
        };

        // 福瑞宝箱 - 完成（C++调用）
        window.onHeavenFuruiComplete = function() {
            window.heavenFuruiRunning = false;
            document.getElementById('start-heaven-furui').disabled = false;
            document.getElementById('stop-heaven-furui').disabled = true;
        };

        // 坐骑大赛 - 一键完成
        window.oneKeyHorseCompetition = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'one_key_horse_competition'
              )HTML" \
R"HTML(  }));
            }
        };

        window.stopHorseCompetition = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'stop_horse_competition'
                }));
            }
        };


        window.updateHorseCompetitionPoints = function(points) {
            var pointsEl = document.getElementById('horse-competition-points');
            if (pointsEl) {
                pointsEl.textContent = points;
            }
        };

        // 状态行：#horse-competition-status 此前从来没人写过，永远显示「就绪」，
        // 用户看不出流程是卡住了还是在跑。C++ 侧 HandleHorseCompetitionProgressCommand
        // 会把每条进度文案连同 running/idle 标记一起推过来（见 web_message_handler.cpp）。
        window.updateHorseCompetitionStatus = function(text, phase) {
            var statusEl = document.getElementById('horse-competition-status');
            if (statusEl) {
                statusEl.textContent = text;
                // )HTML" \
R"HTML(跑动中用主色，报错/收工用次级色，一眼能分开。
                var active = (phase === 'running');
                statusEl.style.color = active
                    ? 'var(--color-text-primary)'
                    : 'var(--color-text-tertiary)';
            }
        };

        // 跳舞大赛 - 计数器
        window.danceGameData = {
            completedCount: 0,  // 已完成次数
            maxCount: 3         // 最大次数
        };

        // 一键跳舞
        window.completeDanceTasks = function() {
            if (window.isDailyTaskRunning) return;
            
            if (!document.getElementById('daily-dance').checked) {
                window.updateDailyTaskStatus('请勾选自动跳舞', false);
                return;
            }
            
            // 重置计数，每次点击都从0开始
            window.danceGameData.completedCount = 0;
            var el = document.getElementById('dance-count');
            if (el) {
                el.textContent = '0';
            }
            
            window.updateDailyTaskStatus('正在执行跳舞大赛...', true)HTML" \
R"HTML();
            window.updateHelperText('正在执行跳舞大赛...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'daily_tasks',
                    flags: 0x100  // 跳舞大赛
                });
            }
        };

        // 更新跳舞次数显示
        window.updateDanceCount = function(count) {
            window.danceGameData.completedCount = count;
            var el = document.getElementById('dance-count');
            if (el) {
                el.textContent = count;
            }
        };

        // 一键采集功能
        window.selectAllCollect = function(checked) {
            for (var i = 0; i < 16; i++) {
                var checkbox = document.getElementById('collect-' + i);
                if (checkbox) {
                    checkbox.checked = checked;
                }
            }
        };

        window.startOneKeyCollect = function() {
            // 收集选中的采集物品
       )HTML" \
R"HTML(     var flags = 0;
            for (var i = 0; i < 16; i++) {
                var checkbox = document.getElementById('collect-' + i);
                if (checkbox && checkbox.checked) {
                    flags |= (1 << i);
                }
            }
            
            if (flags === 0) {
                window.updateHelperText('请至少勾选一项采集物品');
                return;
            }
            
            window.updateHelperText('开始一键采集...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'one_key_collect',
                    flags: flags
                });
            }
        };

        // 跳舞完成回调 - 计数由服务器响应更新，这里只更新状态提示
        window.onDanceComplete = function() {
            // 计数已由 updateDanceCount 更新（从服务器响应获取）
            var count = window.danceGameData.completedCount;
            
            if (count >= window.danceGameData.maxCount) {
  )HTML" \
R"HTML(              window.updateDailyTaskStatus('跳舞大赛已完成全部3次', false);
                window.updateHelperText('跳舞大赛已完成全部3次');
            } else {
                window.updateDailyTaskStatus('跳舞完成，已完成 ' + count + '/3 次', false);
                window.updateHelperText('跳舞完成，已完成 ' + count + '/3 次');
            }
        };

        // 辅助日志管理
        window.helperLogData = [];

        // 自动滚到底部：合并到下一帧，避免每条日志都触发一次强制同步布局
        // （日志列表是滚动容器，逐条读 scrollHeight 会累积成 O(n^2)）
        window.scheduleHelperLogScroll = function() {
            if (window.__helperLogScrollPending) return;
            window.__helperLogScrollPending = true;
            var run = function() {
                window.__helperLogScrollPending = false;
                var logList = document.getElementById('helper-log-list');
                if (logList) {
                    logList.scrollTop = logList.scrollHeight;
                }
            };
            if (window.requestAnimationFrame) {
                window.re)HTML" \
R"HTML(questAnimationFrame(run);
            } else {
                window.setTimeout(run, 0);
            }
        };

        // 更新日志计数（样式依赖 <b> 高亮数字）
        window.updateHelperLogCount = function() {
            var logCount = document.getElementById('helper-log-count');
            if (logCount) {
                logCount.innerHTML = '共 <b>' + window.helperLogData.length + '</b> 条';
            }
        };

        // 渲染空状态占位（表头是滚动容器的固定首个子元素，这里只增删行与占位）
        window.renderHelperLogEmpty = function() {
            var logList = document.getElementById('helper-log-list');
            if (!logList) return;
            // 移除所有数据行与已有占位，保留表头
            var olds = logList.querySelectorAll('.log-row, .log-empty');
            for (var i = 0; i < olds.length; i++) {
                logList.removeChild(olds[i]);
            }
            var empty = document.createElement('div');
            empty.className = 'log-empty';
            empty.innerHTML =
                '<svg viewBox="0 0 16 16)HTML" \
R"HTML(" xmlns="http://www.w3.org/2000/svg">' +
                    '<path d="M4.4 2.4h4.9l2.9 2.9v8.3a.9.9 0 0 1-.9.9H4.4a.9.9 0 0 1-.9-.9V3.3a.9.9 0 0 1 .9-.9z"/>' +
                    '<path d="M9.3 2.4v2.9h2.9"/>' +
                    '<path d="M5.9 8.7h4.2"/>' +
                    '<path d="M5.9 11.2h4.2"/>' +
                '</svg>' +
                '<span>暂无日志记录</span>';
            logList.appendChild(empty);
        };

        // 添加辅助日志
        window.addHelperLog = function(text) {
            var logList = document.getElementById('helper-log-list');
            if (!logList) return;

            // 如果是第一条日志，移除"暂无日志记录"提示（不动表头）
            if (window.helperLogData.length === 0) {
                var olds = logList.querySelectorAll('.log-row, .log-empty');
                for (var i = 0; i < olds.length; i++) {
                    logList.removeChild(olds[i]);
                }
            }

            // 创建时间戳
            var now = new Date();
            var timeStr = now.ge)HTML" \
R"HTML(tHours().toString().padStart(2, '0') + ':' +
                         now.getMinutes().toString().padStart(2, '0') + ':' +
                         now.getSeconds().toString().padStart(2, '0');

            // 添加到数据数组
            var logItem = {
                index: window.helperLogData.length + 1,
                time: timeStr,
                content: text
            };
            window.helperLogData.push(logItem);

            // 添加到UI：用 textContent 填充内容，避免日志文本破坏表格布局
            var logDiv = document.createElement('div');
            logDiv.className = 'log-row';

            var cellIndex = document.createElement('div');
            cellIndex.className = 'log-cell log-cell-index';
            cellIndex.textContent = logItem.index;

            var cellTime = document.createElement('div');
            cellTime.className = 'log-cell log-cell-time';
            cellTime.textContent = logItem.time;

            var cellText = document.createElement('div');
            cellText.cla)HTML" \
R"HTML(ssName = 'log-cell log-cell-text';
            cellText.textContent = logItem.content;
            cellText.title = logItem.content;

            logDiv.appendChild(cellIndex);
            logDiv.appendChild(cellTime);
            logDiv.appendChild(cellText);

            logList.appendChild(logDiv);
            window.scheduleHelperLogScroll();

            // 更新统计信息
            window.updateHelperLogCount();
        };

        // 清空辅助日志
        window.clearHelperLog = function() {
            window.helperLogData = [];
            window.renderHelperLogEmpty();
            window.updateHelperLogCount();
        };

        // 更新辅助提示文本
        window.updateHelperText = function(text) {
            var el = document.getElementById('helper-text');
            if (el) {
                el.textContent = text;
                el.classList.remove('slide-in');
                void el.offsetWidth; // 触发重绘
                el.classList.add('slide-in');
            }

            // 同时添加到辅助日志
)HTML" \
R"HTML(            window.addHelperLog(text);
        };

        // 日常活动完成回调
        window.onDailyTaskComplete = function(completedCount, totalCount) {
            window.updateDailyTaskStatus('已完成 ' + completedCount + '/' + totalCount + ' 项日常活动', false);
            window.updateHelperText('已完成 ' + completedCount + '/' + totalCount + ' 项日常活动');
        };




        // 绑定所有按钮事件
        document.addEventListener('DOMContentLoaded', function() {
            // 标题栏拖拽功能
            var titleBar = document.querySelector('.title-bar');
            if (titleBar) {
                titleBar.addEventListener('mousedown', function(e) {
                    // 检查是否点击在控制按钮上
                    if (e.target.closest('.title-bar-button')) {
                        return; // 不处理控制按钮区域的拖拽
                    }
                    // 发送拖拽消息
                    window.chrome.webview.postMessage({ type: 'window-drag' });
                });
            }
            
            // 标题栏按钮事件
            var btn)HTML" \
R"HTML(Minimize = document.getElementById('btn-minimize');
            if (btnMinimize) {
                btnMinimize.addEventListener('click', function() {
                    window.chrome.webview.postMessage({ type: 'window-minimize' });
                });
            }
            
            var btnClose = document.getElementById('btn-close');
            if (btnClose) {
                btnClose.addEventListener('click', function() {
                    window.chrome.webview.postMessage({ type: 'window-close' });
                });
            }
            
            // 日常活动按钮
            var completeBtn = document.getElementById('complete-daily-tasks');
            if (completeBtn) {
                completeBtn.addEventListener('click', window.completeDailyTasks);
            }
            var trialBtn = document.getElementById('complete-trial-tasks');
            if (trialBtn) {
                trialBtn.addEventListener('click', window.completeTrialTasks);
            }
         )HTML" \
R"HTML(   var danceBtn = document.getElementById('complete-dance-tasks');
            if (danceBtn) {
                danceBtn.addEventListener('click', window.completeDanceTasks);
            }
            var activityBtn = document.getElementById('complete-activity-tasks');
            if (activityBtn) {
                activityBtn.addEventListener('click', window.completeActivityTasks);
            }
            var startTaskZoneBtn = document.getElementById('start-task-zone');
            if (startTaskZoneBtn) {
                startTaskZoneBtn.addEventListener('click', window.startTaskZone);
            }
            var stopTaskZoneBtn = document.getElementById('stop-task-zone');
            if (stopTaskZoneBtn) {
                stopTaskZoneBtn.addEventListener('click', window.stopTaskZone);
            }
            var huangchengHideCompleted = document.getElementById('huangcheng-hide-completed');
            if (huangchengHideCompleted) {
                huangchengHideCompleted.addE)HTML" \
R"HTML(ventListener('change', function() {
                    window.toggleHuangchengWeijiHideCompleted(this.checked);
                });
            }
            var refreshHuangchengBtn = document.getElementById('refresh-huangcheng-weiji');
            if (refreshHuangchengBtn) {
                refreshHuangchengBtn.addEventListener('click', window.refreshHuangchengWeijiTaskProgress);
            }
            var selectAllHuangchengBtn = document.getElementById('select-all-huangcheng-weiji');
            if (selectAllHuangchengBtn) {
                selectAllHuangchengBtn.addEventListener('click', window.selectAllHuangchengWeiji);
            }
            var clearHuangchengBtn = document.getElementById('clear-huangcheng-weiji');
            if (clearHuangchengBtn) {
                clearHuangchengBtn.addEventListener('click', window.clearHuangchengWeijiSelection);
            }
            var startHuangchengBtn = document.getElementById('start-huangcheng-weiji');
            if (star)HTML" \
R"HTML(tHuangchengBtn) {
                startHuangchengBtn.addEventListener('click', window.startHuangchengWeijiTask);
            }
            var stopHuangchengBtn = document.getElementById('stop-huangcheng-weiji');
            if (stopHuangchengBtn) {
                stopHuangchengBtn.addEventListener('click', window.stopHuangchengWeijiTask);
            }
            var xuanttaBtn = document.getElementById('one-key-xuantta');
            if (xuanttaBtn) {
                xuanttaBtn.addEventListener('click', window.oneKeyXuantta);
            }
            var buyDice18Btn = document.getElementById('buy-dice-18');
            if (buyDice18Btn) {
                buyDice18Btn.addEventListener('click', window.buyDice18);
            }
            var startShuangTaiBtn = document.getElementById('start-shuangtai');
            if (startShuangTaiBtn) {
                startShuangTaiBtn.addEventListener('click', window.startShuangTai);
            }
            var queryShuangTaiBtn = document)HTML" \
R"HTML(.getElementById('query-shuangtai');
            if (queryShuangTaiBtn) {
                queryShuangTaiBtn.addEventListener('click', window.queryShuangTai);
            }
            var stopShuangTaiBtn = document.getElementById('stop-shuangtai');
            if (stopShuangTaiBtn) {
                stopShuangTaiBtn.addEventListener('click', window.stopShuangTai);
            }
            var strawberryBtn = document.getElementById('one-key-strawberry');
            if (strawberryBtn) {
                strawberryBtn.addEventListener('click', window.oneKeyStrawberry);
            }
            var startHeavenFuruiBtn = document.getElementById('start-heaven-furui');
            if (startHeavenFuruiBtn) {
                startHeavenFuruiBtn.addEventListener('click', window.startHeavenFurui);
            }
            var stopHeavenFuruiBtn = document.getElementById('stop-heaven-furui');
            if (stopHeavenFuruiBtn) {
                stopHeavenFuruiBtn.addEventListener('click', win)HTML" \
R"HTML(dow.stopHeavenFurui);
            }
            var horseCompetitionBtn = document.getElementById('one-key-horse-competition');
            if (horseCompetitionBtn) {
                horseCompetitionBtn.addEventListener('click', window.oneKeyHorseCompetition);
            }
            var stopHorseCompetitionBtn = document.getElementById('stop-horse-competition');
            if (stopHorseCompetitionBtn) {
                stopHorseCompetitionBtn.addEventListener('click', window.stopHorseCompetition);
            }
            if (typeof window.renderHuangchengWeijiTasks === 'function') {
                window.renderHuangchengWeijiTasks();
            }
        });


        window.applyLingyuSort = function() {
            if (!window.currentLingyuData || !window.currentLingyuData.items) return;
            
            const attr = document.getElementById('lingyu-sort-attr').value;
            // 默认按从高到低排序，如果用户想要更多选项后续可以再加
            const order = 'desc'; 
            
            )HTML" \
R"HTML(let sortedItems = [...window.currentLingyuData.items];
            
            if (attr !== 'none') {
                sortedItems.sort((a, b) => {
                    const valA = (a.nativeList.find(n => n.nativeName === attr) || {nativeValue: 0}).nativeValue;
                    const valB = (b.nativeList.find(n => n.nativeName === attr) || {nativeValue: 0}).nativeValue;
                    return order === 'desc' ? valB - valA : valA - valB;
                });
            }
            
            window.renderLingyuList(sortedItems);
        };

        // 封包窗口显示/隐藏功能
        var packetWindow = document.getElementById('packet-window');
        var togglePacketBtn = document.getElementById('toggle-packet-window');
        var closePacketBtn = document.getElementById('close-packet-window');
        var clearAllBtn = document.getElementById('clear-all-packets');
        var deleteSelectedBtn = document.getElementById('delete-selected-packets');
        
        // 显示/隐藏封包窗口
        )HTML" \
R"HTML(if (togglePacketBtn) {
            togglePacketBtn.addEventListener('click', function() {
                if (packetWindow.style.display === 'none' || packetWindow.style.display === '') {
                    // 显示窗口
                    packetWindow.style.display = 'block';
                    // 添加动画效果
                    setTimeout(function() {
                        packetWindow.style.opacity = '1';
                        packetWindow.style.transform = 'translateY(0)';
                        // 获取窗口位置和大小
                        var rect = packetWindow.getBoundingClientRect();
                        // 发送postMessage通知C++端弹窗显示
                        if (window.chrome && window.chrome.webview) {
                            window.chrome.webview.postMessage({
                                type: 'packet_window_visible',
                                visible: true,
                                left: rect.left,
                                top: rect.top,
                     )HTML" \
R"HTML(           width: rect.width,
                                height: rect.height
                            });
                        }
                    }, 10);
                    // 更改按钮文本
                    togglePacketBtn.textContent = '关闭';
                } else {
                    // 隐藏窗口
                    packetWindow.style.opacity = '0';
                    // 添加动画效果
                    setTimeout(function() {
                        packetWindow.style.display = 'none';
                        // 更改按钮文本
                        togglePacketBtn.textContent = '显示封包';
                        // 发送postMessage通知C++端弹窗隐藏
                        if (window.chrome && window.chrome.webview) {
                            window.chrome.webview.postMessage({
                                type: 'packet_window_visible',
                                visible: false
                            });
                        }
                    }, 200);
                    packet)HTML" \
R"HTML(Window.style.transform = 'translateY(0)';
                }
            });
        }
        
        // 关闭封包窗口
        if (closePacketBtn) {
            closePacketBtn.addEventListener('click', function() {
                packetWindow.style.opacity = '0';
                // 添加动画效果
                setTimeout(function() {
                    packetWindow.style.display = 'none';
                    // 更改按钮文本
                    togglePacketBtn.textContent = '显示封包';
                    // 发送postMessage通知C++端弹窗隐藏
                    if (window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage({
                            type: 'packet_window_visible',
                            visible: false
                        });
}
                }, 200);
                packetWindow.style.transform = 'translateY(0)';
            });
        }
        
        // 清空所有封包
        if (clearAllBtn) {
            clearAllBtn.addEventListener('click', functi)HTML" \
R"HTML(on() {
                // 清空逻辑已内置在HTML的onclick属性中
            });
        }

        // 删除选中封包
        if (deleteSelectedBtn) {
            deleteSelectedBtn.addEventListener('click', function() {
                var pListItems = document.getElementById('packet-list-items');
                if (pListItems) {
                    // 找到所有选中的封包项
                    var selectedItems = pListItems.querySelectorAll('.packet-item[style*="background"]');
                    if (selectedItems.length === 0) {
                        alert('请先选中要删除的封包');
                        return;
                    }

                    // 收集选中的索引
                    var selectedIndices = [];
                    selectedItems.forEach(function(item) {
                        var index = item.getAttribute('data-index');
                        if (index) {
                            selectedIndices.push(parseInt(index));
                        }
                    });

                    // 从UI中删除
      )HTML" \
R"HTML(              selectedItems.forEach(function(item) {
                        item.remove();
                    });

                    // 通知C++端删除封包
                    if (window.chrome && window.chrome.webview && selectedIndices.length > 0) {
                        window.chrome.webview.postMessage({
                            type: 'delete_selected_packets',
                            indices: selectedIndices
                        });
                    }

                    // 更新计数
                    var countElement = document.getElementById('packet-count');
                    if (countElement) {
                        countElement.textContent = pListItems.children.length;
                    }
                }
            });
        }
        
        // 变速功能实现
        var speedSlider = document.getElementById('speed-slider');
        var speedValue = document.getElementById('speed-value');
        
        if (speedSlider) {
            speedSlider.addEventListener)HTML" \
R"HTML(('input', function() {
                var val = parseFloat(this.value).toFixed(1);
                speedValue.textContent = val + 'X';
                
                // 缓动特效：这里可以添加一些UI上的视觉反馈，比如改变阴影或颜色深度
                speedValue.style.transform = 'scale(1.1)';
                setTimeout(() => {
                    speedValue.style.transform = 'scale(1)';
                }, 100);

                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'set_speed',
                        speed: parseFloat(val)
                    });
                }
            });
        }
        
        // 自动回血功能实现
        window.toggleAutoHeal = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'toggle_auto_heal',
                    enabled: enabled
                });
            }
        };

        // 万妖盛会 /)HTML" \
R"HTML( 八卦灵盘共用自动战斗功能实现
        window.toggleBattleSixAutoBattle = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'battlesix_set_auto_battle',
                    enabled: enabled
                });
            }
        };

        // 屏蔽战斗功能实现
        window.toggleBlockBattle = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'set_block_battle',
                    enabled: enabled
                });
            }
        };

        // 自动回家功能实现
        window.toggleAutoGoHome = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'set_auto_go_home',
                    enabled: enabled
                });
            }
        };

        // 封包列表功能实现
        var packetList = d)HTML" \
R"HTML(ocument.getElementById('packet-list');
        var packetHexInput = document.getElementById('packet-hex-input');
        var sendPacketBtn = document.getElementById('send-packet-btn');
        var startInterceptBtn = document.getElementById('start-intercept');
        var stopInterceptBtn = document.getElementById('stop-intercept');
        var interceptSendCb = document.getElementById('intercept-send');
        var interceptReceiveCb = document.getElementById('intercept-receive');
        var clearAllBtn = document.getElementById('clear-all-packets');
        var deleteSelectedBtn = document.getElementById('delete-selected-packets');
        var packetCountElement = document.getElementById('packet-count');
        
        // 点击列表项时的选中效果和数据填充
        if (packetList) {
            packetList.addEventListener('click', function(e) {
                var pListItems = document.getElementById('packet-list-items');
                if (!pListItems) return;
                
                var )HTML" \
R"HTML(clickedItem = e.target.closest('.packet-item');
                if (clickedItem) {
                    // 移除之前的选中状态
                    var allItems = pListItems.querySelectorAll('.packet-item');
                    allItems.forEach(function(item) {
                        item.classList.remove('selected');
                    });
                    
                    // 添加当前项的选中状态
                    clickedItem.classList.add('selected');
                    
                    // 将16进制字符串显示到编辑框
                    var hexData = clickedItem.getAttribute('data-hex');
                    if (packetHexInput) {
                        packetHexInput.value = hexData;
                    }
                }
            });
        }
        
        // 发送封包功能
        if (sendPacketBtn) {
            console.log('[Debug] 发送按钮已找到，绑定事件监听器');
            sendPacketBtn.addEventListener('click', function(e) {
                console.log('[Debug] 发送按钮被点击');
                e.preventDefault();
)HTML" \
R"HTML(                var hexData = packetHexInput ? packetHexInput.value : '';
                console.log('[Debug] 封包数据: ' + hexData);
                if (!hexData.trim()) {
                    window.updateHelperText('请输入要发送的封包数据');
                    return;
                }
                // 发送postMessage通知C++端发送封包
                if (window.chrome && window.chrome.webview) {
                    console.log('[Debug] 发送postMessage到C++端');
                    window.chrome.webview.postMessage({
                        type: 'send_packet',
                        hex: hexData
                    });
                    // 显示发送提示（C++端会处理实际发送结果）
                    window.updateHelperText('封包已发送');
                } else {
                    console.log('[Debug] window.chrome.webview 不存在');
                }
            });
        } else {
            console.log('[Debug] 发送按钮未找到');
        }
        
        // 删除选中封包功能
        if (deleteSelectedBtn) {
            deleteSelectedBtn.add)HTML" \
R"HTML(EventListener('click', function() {
                var pListItems = document.getElementById('packet-list-items');
                if (!pListItems) return;
                
                var selectedItems = pListItems.querySelectorAll('.packet-item.selected');
                var indices = [];
                selectedItems.forEach(function(item) {
                    var index = parseInt(item.getAttribute('data-index'), 10);
                    if (!isNaN(index)) {
                        indices.push(index - 1);
                    }
                    item.remove();
                });
                // 清空编辑框
                if (packetHexInput) {
                    packetHexInput.value = '';
                }
                
                // 更新封包计数
                updatePacketCount();
                
                // 通知后端删除这些索引
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type:)HTML" \
R"HTML( 'delete_selected_packets',
                        indices: indices
                    });
                }
            });
        }

        // 开始拦截按钮
        if (startInterceptBtn) {
            startInterceptBtn.addEventListener('click', function() {
                // 发送postMessage通知C++端开始拦截
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('start_intercept');
                }

                // 更新按钮状态
                startInterceptBtn.style.background = 'var(--color-background)';
                startInterceptBtn.style.color = 'var(--color-text-primary)';
                startInterceptBtn.style.borderColor = 'var(--color-border)';
                startInterceptBtn.disabled = true;

                stopInterceptBtn.style.background = 'var(--color-primary)';
                stopInterceptBtn.style.color = 'white';
                stopInterceptBtn.style.borderColor = 'var(--color-primary)';
                stopInt)HTML" \
R"HTML(erceptBtn.disabled = false;
            });
        }

        // 停止拦截按钮
        if (stopInterceptBtn) {
            stopInterceptBtn.addEventListener('click', function() {
                // 发送postMessage通知C++端停止拦截
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('stop_intercept');
                }

                // 更新按钮状态
                stopInterceptBtn.style.background = 'var(--color-background)';
                stopInterceptBtn.style.color = 'var(--color-text-primary)';
                stopInterceptBtn.style.borderColor = 'var(--color-border)';
                stopInterceptBtn.disabled = true;

                startInterceptBtn.style.background = 'var(--color-primary)';
                startInterceptBtn.style.color = 'white';
                startInterceptBtn.style.borderColor = 'var(--color-primary)';
                startInterceptBtn.disabled = false;
            });
        }

        // 拦截类型复选框
        if ()HTML" \
R"HTML(interceptSendCb && interceptReceiveCb) {
            interceptSendCb.addEventListener('change', function() {
                updateInterceptType();
            });

            interceptReceiveCb.addEventListener('change', function() {
                updateInterceptType();
            });
        }

        // 更新拦截类型函数
        function updateInterceptType() {
            if (interceptSendCb && interceptReceiveCb) {
                // 发送postMessage通知C++端更新拦截类型
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'set_intercept_type',
                        send: interceptSendCb.checked,
                        recv: interceptReceiveCb.checked
                    });
                }
            }
        }

// 清空封包列表
        if (clearAllBtn) {
            clearAllBtn.addEventListener('click', function() {
                var pListItems = document.getElementById('packet-list-items');
       )HTML" \
R"HTML(         if (pListItems) {
                    pListItems.innerHTML = '';
                }
                
                // 清空编辑框
                if (packetHexInput) {
                    packetHexInput.value = '';
                }
                
                // 更新封包计数
                updatePacketCount();
                
                // 发送postMessage通知C++端清空封包
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('clear_packets');
                }
            });
        }

        // 更新封包计数函数
        window.updatePacketCount = function(count) {
            var countElement = document.getElementById('packet-count');
            var packetListItems = document.getElementById('packet-list-items');
            if (countElement) {
                if (count !== undefined) {
                    countElement.textContent = count;
                } else {
                    var packetCount = packetListItems ? packetLis)HTML" \
R"HTML(tItems.children.length : 0;
                    countElement.textContent = packetCount;
                }
            }
        };

        // ==================== 劫持功能实现 ====================
        var hijackEnabledCb = document.getElementById('hijack-enabled');
        var hijackSendCb = document.getElementById('hijack-send');
        var hijackRecvCb = document.getElementById('hijack-recv');
        var hijackPatternInput = document.getElementById('hijack-pattern');
        var hijackReplaceInput = document.getElementById('hijack-replace');
        var hijackTypeSelect = document.getElementById('hijack-type');

        // 自动劫持功能 - 当复选框状态改变时自动发送劫持设置
        function sendHijackSettings() {
            var pattern = hijackPatternInput ? hijackPatternInput.value.trim() : '';
            var replace = hijackReplaceInput ? hijackReplaceInput.value.trim() : '';
            var isSend = hijackSendCb ? hijackSendCb.checked : true;
            var isRecv = hijackRecvCb ? hijackRecvCb.checked)HTML" \
R"HTML( : true;
            var hijackType = hijackTypeSelect ? hijackTypeSelect.value : 'block';

            // 如果没有检测文本，则清空劫持规则（以前是直接 return，
            // 规则会一直留在后台，删掉内容等于没删）
            if (!pattern) {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('clear_hijack_rules');
                }
                return;
            }

            // 如果是替换模式但没有替换文本，同样清空规则（留着旧规则会误伤）
            if (hijackType === 'replace' && !replace) {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('clear_hijack_rules');
                }
                return;
            }

            // 如果两个复选框都没有选中，则清空劫持规则
            if (!isSend && !isRecv) {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('clear_hijack_rules');
                }
                return;
            }

            // 发送劫持规则
            )HTML" \
R"HTML(if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'add_hijack_rule',
                    rule: {
                        isSend: isSend,
                        isRecv: isRecv,
                        isBlock: (hijackType === 'block'),
                        pattern: pattern,
                        replace: replace
                    }
                });
            }
        }

        // 为复选框添加 change 事件监听器
        if (hijackSendCb) {
            hijackSendCb.addEventListener('change', sendHijackSettings);
        }

        if (hijackRecvCb) {
            hijackRecvCb.addEventListener('change', sendHijackSettings);
        }

        // 为输入框添加 input 事件监听器，实时更新劫持设置
        if (hijackPatternInput) {
            hijackPatternInput.addEventListener('input', sendHijackSettings);
        }

        if (hijackReplaceInput) {
            hijackReplaceInput.addEventListener('input', sendHijackSettings);
        }

)HTML" \
R"HTML(        // 为劫持类型选择框添加 change 事件监听器
        if (hijackTypeSelect) {
            hijackTypeSelect.addEventListener('change', sendHijackSettings);
        }

        // ==================== 保存/载入封包功能实现 ====================
        var savePacketsBtn = document.getElementById('save-packets');
        var loadPacketsBtn = document.getElementById('load-packets');
        var sendAllPacketsBtn = document.getElementById('start-send');

        // 保存封包
        if (savePacketsBtn) {
            savePacketsBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    // 直接通知C++端打开保存对话框
                    window.chrome.webview.postMessage({
                        type: 'save_packet_list'
                    });
                }
            });
        }

        // 载入封包
        if (loadPacketsBtn) {
            loadPacketsBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
       )HTML" \
R"HTML(             // 直接通知C++端打开载入对话框
                    window.chrome.webview.postMessage({
                        type: 'load_packet_list'
                    });
                }
            });
        }

        // 自动发送所有封包
        if (sendAllPacketsBtn) {
            sendAllPacketsBtn.addEventListener('click', function() {
                // 获取发送设置
                var sendCountInput = document.getElementById('send-count');
                var sendDelayInput = document.getElementById('send-delay');
                var sendCount = sendCountInput ? parseInt(sendCountInput.value) || 1 : 1;
                var sendDelay = sendDelayInput ? parseInt(sendDelayInput.value) || 300 : 300;

                // 发送配置到C++端
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'send_all_packets',
                        sendCount: sendCount,
                        sendDelay: sendDelay
                    })HTML" \
R"HTML();
                }
            });
        }

        // 停止发送封包
        var stopSendBtn = document.getElementById('stop-send');
        if (stopSendBtn) {
            stopSendBtn.addEventListener('click', function() {
                // 通知C++端停止发送
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'stop_send'
                    });
                }
            });
        }

        // 监听来自C++的消息，用于添加封包
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.addEventListener('message', function(event) {
                if (event.data && typeof event.data === 'object' && event.data.type === 'add_packet') {
                    addPacketToUI(event.data.direction, event.data.hex, event.data.time, event.data.label || '');
                }
            });
        }

        // 初始页面加载时，触发第一个标签页的动画
        window.onload = function() {
            console.log('[U)HTML" \
R"HTML(I] Page loaded, checking window functions...');
            console.log('[UI] window.updateBattleUI:', typeof window.updateBattleUI);
            console.log('[UI] window.addBattleData:', typeof window.addBattleData);
            
            var activeTab = document.querySelector('.tab-content.active');
            if (activeTab) {
                reanimateTabContent(activeTab);
            }
        };

        /**
         * 简化 Buff 名称显示
         * @param {string} name - 原始名称
         * @param {number} round - 回合数
         * @returns {string} 简化后的名称
         */
        // 状态名压缩：'速度提升3级' -> '速↑3'，'防御力下降' -> '防↓'，'攻击力提升' -> '攻↑'
        const BUF_STAT_LABELS = {
            '攻击力': '攻', '攻击': '攻', '防御力': '防', '防御': '防',
            '速度': '速', '命中率': '命中', '命中': '命中', '暴击': '暴击',
            '法术': '法', '抗性': '抗', '体力': '体'
        };

        function simplifyBufName(name, round) {
            if (!name) return '状态';

            let text = String(name).replace(/\s+/g, '');
            )HTML" \
R"HTML(const m = text.match(/^(攻击力|防御力|命中率|攻击|防御|速度|命中|暴击|法术|抗性|体力)(提升|下降|降低)(\d+)?级?/);
            if (m) {
                const label = BUF_STAT_LABELS[m[1]] || m[1];
                const arrow = m[2] === '提升' ? '↑' : '↓';
                const lv = m[3] ? Number(m[3]) : 0;
                // 属性增减状态在游戏里固定是 1~6 级，超出范围一律视为异常值，不显示数字。
                text = label + arrow + (lv >= 1 && lv <= 6 ? String(lv) : '');
            } else {
                text = text.replace(/提升|增加/g, '↑')
                           .replace(/下降|降低|减少/g, '↓')
                           .replace(/级$/, '');
            }

            if (round > 0) text += '(' + round + ')';
            return text;
        }

        /**
         * 渲染 Buff 标签列表
         * @param {Array} bufArr - Buff 数组
         * @param {HTMLElement} container - 容器元素
         */
        function renderBufList(bufArr, container) {
            container.innerHTML = '';
            if (!bufArr || bufArr.length === 0) {
                container.classL)HTML" \
R"HTML(ist.remove('has-bufs');
                return;
            }

            // 用于去重的 Map：key = bufId, value = buf 对象
            const bufMap = new Map();
            
            // 首先处理去重：同类型 Buff 只保留最新的
            bufArr.forEach(buf => {
                // 跳过瞬时/PP 事件 (3, 4, 5, 6)，这些不显示为状态标签
                if (buf.addOrRemove === 3 || buf.addOrRemove === 4 ||
                    buf.addOrRemove === 5 || buf.addOrRemove === 6) {
                    return;
                }

                // 移除类型 (0)：从 Map 中删除
                if (buf.addOrRemove === 0) {
                    if (buf.bufId) {
                        bufMap.delete(buf.bufId);
                    }
                    return;
                }

                // 添加/更新类型（addOrRemove 为 1 或 2）：放入 Map
                // 注意：回合开始时 addOrRemove 可能保持为 1，这表示已存在的 buf
                if (buf.addOrRemove === 1 || buf.addOrRemove === 2) {
                    if (buf.bufId) {
                        bufMap.set(buf.bufId, buf);
     )HTML" \
R"HTML(               }
                    return;
                }

                // 如果 addOrRemove 未设置或为其他值（回合开始更新的 buf），根据 bufId 处理
                if (buf.bufId) {
                    // 更新 Map 中已有的 buf
                    bufMap.set(buf.bufId, buf);
                }
            });

            // 有状态才展开这一行（CSS 里高度 0 -> 17px），避免空白挤占技能区
            container.classList.toggle('has-bufs', bufMap.size > 0);

            // 渲染去重后的 Buff
            bufMap.forEach(buf => {
                const tag = document.createElement('span');
                tag.className = 'buf-tag';
                
                // 根据 bufId 判断是增益还是减益
                const buffIds = [2, 9, 17, 24, 29, 33, 34, 59, 95, 36, 37, 46, 45, 62, 9999];
                const isBuff = buffIds.includes(buf.bufId) || 
                               (buf.name && (
                                   buf.name.includes('攻击') && !buf.name.includes('降') && !buf.name.includes('减') ||
                                   buf.name.inclu)HTML" \
R"HTML(des('防御') && !buf.name.includes('降') && !buf.name.includes('减') ||
                                   buf.name.includes('速度') && !buf.name.includes('降') && !buf.name.includes('减') ||
                                   buf.name.includes('命中') ||
                                   buf.name.includes('暴击') ||
                                   buf.name.includes('回血') ||
                                   buf.name.includes('护盾') ||
                                   buf.name.includes('提升')
                               ));
                
                // 减益关键词
                const debuffKeywords = ['灼烧', '中毒', '流血', '麻痹', '冰冻', '睡眠', '混乱', '恐惧', '诅咒', '虚弱', '减速', '降低'];
                const isDebuff = debuffKeywords.some(kw => buf.name && buf.name.includes(kw));

                // 设置样式
                if (isDebuff) {
                    tag.style.background = 'rgba(244, 67, 54, 0.15)';
                    tag.style.color = '#d32f2f';
                    tag.style.border = '1px solid)HTML" \
R"HTML( rgba(244, 67, 54, 0.3)';
                } else if (isBuff) {
                    tag.style.background = 'rgba(76, 175, 80, 0.15)';
                    tag.style.color = '#388e3c';
                    tag.style.border = '1px solid rgba(76, 175, 80, 0.3)';
                } else {
                    tag.style.background = 'rgba(158, 158, 158, 0.15)';
                    tag.style.color = '#616161';
                    tag.style.border = '1px solid rgba(158, 158, 158, 0.3)';
                }

                tag.style.fontSize = '8.5px';
                tag.style.padding = '1px 4px';
                tag.style.borderRadius = '2px';
                tag.style.cursor = 'pointer';
                tag.style.whiteSpace = 'nowrap';
                tag.style.flexShrink = '0';  // 防止标签被压缩

                // 简化显示名称
                const displayText = simplifyBufName(buf.name, buf.round);
                tag.textContent = displayText;

                // 构建 tooltip 内容（显示完整信息）
                let)HTML" \
R"HTML( tooltipContent = '';
                if (buf.name) {
                    tooltipContent += '<b>' + buf.name + '</b>';
                }
                if (buf.round > 0) {
                    tooltipContent += '<br>剩余: ' + buf.round + ' 回合';
                }
                if (buf.tipString) {
                    tooltipContent += '<br>' + buf.tipString;
                }
                if (typeof buf.param1 === 'number' && Math.abs(buf.param1) > 99) {
                    tooltipContent += '<br><span style="color:#888;font-size:8px;">数值: ' + buf.param1 + '</span>';
                }
                if (buf.bufId) {
                    tooltipContent += '<br><span style="color:#888;font-size:8px;">ID: ' + buf.bufId + '</span>';
                }

                if (tooltipContent) {
                    tag.title = tooltipContent.replace(/<br>/g, '\n').replace(/<[^>]*>/g, '');
                    tag.setAttribute('data-tooltip', tooltipContent);
                    
               )HTML" \
R"HTML(     // 添加悬停事件显示详细 tooltip
                    tag.addEventListener('mouseenter', function(e) {
                        showBufTooltip(e, this);
                    });
                    tag.addEventListener('mouseleave', function() {
                        hideBufTooltip();
                    });
                }

                container.appendChild(tag);
            });
            
            // 延迟计算宽度，确保标签已完全渲染
            setTimeout(() => {
                // 计算所有标签的总宽度
                let totalWidth = 0;
                const tags = container.querySelectorAll('.buf-tag');
                tags.forEach(tag => {
                    totalWidth += tag.offsetWidth;
                });
                const gapWidth = 2; // gap: 2px
                const gapCount = Math.max(0, tags.length - 1);
                totalWidth += gapCount * gapWidth;
                
                const containerWidth = container.clientWidth;
                console.log('buf宽度计算 - 总宽度:', totalWidth,)HTML" \
R"HTML( '容器宽度:', containerWidth, '标签数量:', tags.length);
                
                // 只有当总宽度超过容器宽度时才显示滚动条
                if (totalWidth > containerWidth) {
                    container.style.overflowX = 'auto';
                    container.classList.add('overflow-auto');
                    console.log('✓ 显示滚动条');
                } else {
                    container.style.overflowX = 'hidden';
                    container.classList.remove('overflow-auto');
                    console.log('✓ 隐藏滚动条');
                }
            }, 10);  // 延迟10ms确保DOM渲染完成
        }

        /**
         * 显示 Buff 详细 Tooltip
         */
        function showBufTooltip(event, element) {
            hideBufTooltip(); // 先移除已有的

            const tooltip = document.createElement('div');
            tooltip.id = 'buf-tooltip';
            tooltip.style.cssText = `
                position: fixed;
                background: rgba(40, 40, 40, 0.95);
                color: #fff;
                padding: )HTML" \
R"HTML(8px 10px;
                border-radius: 4px;
                font-size: 10px;
                line-height: 1.5;
                max-width: 200px;
                z-index: 10000;
                box-shadow: 0 2px 8px rgba(0,0,0,0.3);
                pointer-events: none;
            `;

            const content = element.getAttribute('data-tooltip');
            if (content) {
                tooltip.innerHTML = content;
            }

            document.body.appendChild(tooltip);

            // 定位
            const rect = element.getBoundingClientRect();
            let left = rect.left;
            let top = rect.bottom + 4;

            // 防止超出屏幕
            if (left + tooltip.offsetWidth > window.innerWidth) {
                left = window.innerWidth - tooltip.offsetWidth - 8;
            }
            if (top + tooltip.offsetHeight > window.innerHeight) {
                top = rect.top - tooltip.offsetHeight - 4;
            }

            tooltip.style.left = left + 'px';
   )HTML" \
R"HTML(         tooltip.style.top = top + 'px';
        }

        /**
         * 隐藏 Buff Tooltip
         */
        function hideBufTooltip() {
            const existing = document.getElementById('buf-tooltip');
            if (existing) {
                existing.remove();
            }
        }

        /**
         * 解析出战序号
         * C++ 侧解析失败时可能给出 -1（无出战宠物），直接拿它去索引会退化成队首，
         * 表现就是"切换宠物后 UI 不跟着换"。这里补一层兜底：
         * 先看序号是否合法，再退回 state===1（在场）的宠物，最后才用第 0 只。
         */
        function resolveActiveIndex(pets, index) {
            if (!Array.isArray(pets) || pets.length === 0) return -1;
            if (typeof index === 'number' && index >= 0 && index < pets.length) return index;
            for (let i = 0; i < pets.length; i++) {
                const pet = pets[i];
                if (pet && !pet.placeholder && pet.state === 1) return i;
            }
            for (let i = 0; i < pets.length; i++) {
                if (pets[i] && !pets[i].placeholder) return i;
            )HTML" \
R"HTML(}
            return 0;
        }

        /**
         * 更新战斗UI数据
         * @param {Object} data - 战斗数据对象
         */
        window.updateBattleUI = function(data) {
            if (!data) data = window._battleData;
            if (!data) return;
            window._battleData = data;
            
            // 调试输出：确认函数被调用
            console.log('[Battle] updateBattleUI called with data:', data);

            // 更新我方数据
            if (data.myPets && data.myPets.length > 0) {
                const myIdx = resolveActiveIndex(data.myPets, data.myActiveIndex);
                const myActive = data.myPets[myIdx] || data.myPets[0];
                document.getElementById('my-spirit-id').textContent = myActive.spiritId;
                document.getElementById('my-hp').textContent = myActive.hp + '/' + myActive.maxHp;
                document.getElementById('my-name').textContent = myActive.name ? myActive.name : ('妖怪' + myActive.spiritId);

                // 更新技能列表
                cons)HTML" \
R"HTML(t mySkillList = document.getElementById('my-skill-list');
                mySkillList.innerHTML = '';
                if (!(Array.isArray(myActive.skills) && myActive.skills.length > 0)) {
                    mySkillList.innerHTML = '<div class="flat-empty">暂无技能数据</div>';
                }
                (Array.isArray(myActive.skills) ? myActive.skills : []).forEach(skill => {
                    const row = document.createElement('div');
                    row.className = 'skill-row';
                    row.innerHTML = `
                        <div>${skill.name ? skill.name : ('技能' + skill.id)}</div>
                        <div>${skill.pp}/${skill.maxPp}</div>
                    `;
                    mySkillList.appendChild(row);
                });

                // 更新我方 Buff 标签
                const myBufList = document.getElementById('my-buf-list');
                if (myBufList) {
                    renderBufList(myActive.bufArr, myBufList);
                }

         )HTML" \
R"HTML(       // 更新我方组合框
                const mySelect = document.getElementById('my-pet-select');
                mySelect.innerHTML = '';
                data.myPets.forEach((pet, index) => {
                    const option = document.createElement('option');
                    option.value = index;
                    option.textContent = pet.name ? pet.name : ('妖怪' + pet.spiritId);
                    if (index === myIdx) option.selected = true;
                    mySelect.appendChild(option);
                });
                mySelect.value = String(myIdx);
            } else {
                // Clear UI
                document.getElementById('my-spirit-id').textContent = '0';
                document.getElementById('my-hp').textContent = '0/0';
                document.getElementById('my-name').textContent = '未知';
                document.getElementById('my-skill-list').innerHTML = '';
                document.getElementById('my-pet-select').innerHTML = '<option value="0">妖怪1</o)HTML" \
R"HTML(ption>';
                const myBufList = document.getElementById('my-buf-list');
                if (myBufList) myBufList.innerHTML = '';
            }

            // 更新敌方数据
            if (data.otherPets && data.otherPets.length > 0) {
                const otherIdx = resolveActiveIndex(data.otherPets, data.otherActiveIndex);
                const otherActive = data.otherPets[otherIdx] || data.otherPets[0];
                document.getElementById('other-spirit-id').textContent = otherActive.spiritId;
                document.getElementById('other-hp').textContent = otherActive.hp + '/' + otherActive.maxHp;
                document.getElementById('other-name').textContent = otherActive.name ? otherActive.name : ('妖怪' + otherActive.spiritId);

                // 更新技能列表
                const otherSkillList = document.getElementById('other-skill-list');
                otherSkillList.innerHTML = '';
                if (!(Array.isArray(otherActive.skills) && otherActive.skills.length > )HTML" \
R"HTML(0)) {
                    otherSkillList.innerHTML = '<div class="flat-empty">暂无技能数据</div>';
                }
                (Array.isArray(otherActive.skills) ? otherActive.skills : []).forEach(skill => {
                    const row = document.createElement('div');
                    row.className = 'skill-row';
                    row.innerHTML = `
                        <div>${skill.name ? skill.name : ('技能' + skill.id)}</div>
                        <div>${skill.pp}/${skill.maxPp}</div>
                    `;
                    otherSkillList.appendChild(row);
                });

                // 更新敌方 Buff 标签
                const otherBufList = document.getElementById('other-buf-list');
                if (otherBufList) {
                    renderBufList(otherActive.bufArr, otherBufList);
                }

                // 更新敌方组合框
                const otherSelect = document.getElementById('other-pet-select');
                otherSelect.innerHTML = '';
             )HTML" \
R"HTML(   data.otherPets.forEach((pet, index) => {
                    const option = document.createElement('option');
                    option.value = index;
                    option.textContent = pet.name ? pet.name : ('妖怪' + pet.spiritId);
                    if (index === otherIdx) option.selected = true;
                    otherSelect.appendChild(option);
                });
                otherSelect.value = String(otherIdx);
            } else {
                // Clear UI
                document.getElementById('other-spirit-id').textContent = '0';
                document.getElementById('other-hp').textContent = '0/0';
                document.getElementById('other-name').textContent = '未知';
                document.getElementById('other-skill-list').innerHTML = '';
                document.getElementById('other-pet-select').innerHTML = '<option value="0">敌方妖怪1</option>';
                const otherBufList = document.getElementById('other-buf-list');
                if (otherB)HTML" \
R"HTML(ufList) otherBufList.innerHTML = '';
            }
        };

        (function() {
            var mySelectEl = document.getElementById('my-pet-select');
            if (mySelectEl) {
                mySelectEl.addEventListener('change', function() {
                    var d = window._battleData;
                    if (!d || !Array.isArray(d.myPets) || d.myPets.length === 0) return;
                    var idx = parseInt(this.value, 10);
                    if (isNaN(idx)) idx = 0;
                    if (idx < 0) idx = 0;
                    if (idx >= d.myPets.length) idx = d.myPets.length - 1;
                    d.myActiveIndex = idx;
                    window.updateBattleUI(d);
                });
            }
            var otherSelectEl = document.getElementById('other-pet-select');
            if (otherSelectEl) {
                otherSelectEl.addEventListener('change', function() {
                    var d = window._battleData;
                    if (!d || !Array.isAr)HTML" \
R"HTML(ray(d.otherPets) || d.otherPets.length === 0) return;
                    var idx = parseInt(this.value, 10);
                    if (isNaN(idx)) idx = 0;
                    if (idx < 0) idx = 0;
                    if (idx >= d.otherPets.length) idx = d.otherPets.length - 1;
                    d.otherActiveIndex = idx;
                    window.updateBattleUI(d);
                });
            }
        })();

        /**
         * 添加战斗数据到UI列表
         * @param {string} type - 消息类型 (如: '战斗开始', '回合结算')
         * @param {string} data - 数据内容描述
         */
        window.addBattleData = function(type, data) {
            // 获取我方和敌方的技能列表容器 (这里以我方为例进行演示，实际应根据 type 解析)
            // 假设我们将所有解析到的战斗事件添加到一个通用的记录列表，或者更新现有的属性
            
            // 查找战斗数据标签页中的列表容器
            var battleTab = document.getElementById('battle');
            if (!battleTab) return;

            // 示例：更新辅助提示区域
            var helperText = document.getElementById('helper-text');
            if (helperText) {)HTML" \
R"HTML(
                helperText.textContent = "[" + type + "] " + data;
                helperText.classList.remove('slide-in');
                void helperText.offsetWidth; // 触发重绘
                helperText.classList.add('slide-in');
            }
        };

        // ============== 妖怪背包功能 ==============
        window.currentMonsterData = { monsters: [] };
        window.selectedMonsterIndex = -1;

        // 性格名称映射
        const geniusNames = {
            1: '固执', 2: '保守', 3: '胆小', 4: '开朗', 5: '勇敢',
            6: '冷静', 7: '慎重', 8: '淘气', 9: '孤僻', 10: '调皮',
            11: '急躁', 12: '天真', 13: '温顺', 14: '慎重', 15: '浮躁',
            16: '认真', 17: '坦率', 18: '实干', 19: '害羞', 20: '保守'
        };

        // 性格颜色映射（攻击、防御、法术、抗性、体力、速度）
        const geniusColors = {
            '攻击': '#e74c3c', '防御': '#3498db', '法术': '#9b59b6', 
            '抗性': '#1abc9c', '体力': '#e67e22', '速度': '#2ecc71'
        };

        /**
         * 更新妖怪背包UI
         * @param {Object|string} data - 妖怪数据对象或JSON字符串
     )HTML" \
R"HTML(    */
        window.updateMonsterUI = function(data) {
            try {
                const monsterData = typeof data === 'string' ? JSON.parse(data) : data;
                window.currentMonsterData = monsterData;
                window.renderMonsterList(monsterData.monsters || []);
            } catch (e) {
                console.error('解析妖怪数据失败:', e);
            }
        };

        /**
         * 渲染妖怪列表
         */
        window.renderMonsterList = function(monsters) {
            const listContainer = document.getElementById('monster-list');
            const countElement = document.getElementById('monster-count');
            
            if (!listContainer) return;
            
            listContainer.innerHTML = '';
            if (!monsters || monsters.length === 0) {
                listContainer.innerHTML = '<div class="flat-empty">暂无妖怪数据，请打开游戏妖怪背包</div>';
                if (countElement) countElement.textContent = '0';
                return;
            }
     )HTML" \
R"HTML(       
            if (countElement) countElement.textContent = monsters.length;
            
            monsters.forEach((monster, index) => {
                                const row = document.createElement('div');
                row.className = 'mb-row' + (window.selectedMonsterIndex === index ? ' is-selected' : '');
                row.onclick = () => window.selectMonster(index);
                
                // 获取性格名称
                const geniusName = monster.geniusName || geniusNames[monster.geniusType] || '平衡';
                
                // 获取资质名称
                const aptitudeName = monster.aptitudeName || '未知';
                
                // 获取性别名称
                const sexNames = ['未知', '雌', '雄', '无'];
                const sexName = sexNames[monster.sex] || '未知';
                const sexColor = monster.sex === 1 ? '#d13438' : (monster.sex === 2 ? '#0078d4' : 'var(--color-text-secondary)');
                
                // 格式化技能列表 - 显示所有技能，用/分隔
       )HTML" \
R"HTML(         let skillNames = '';
                if (monster.skills && monster.skills.length > 0) {
                    skillNames = monster.skills.map(s => s.name || ('技能' + s.id)).join(' / ');
                } else {
                    skillNames = '无';
                }
                
                // 判断是否首发
                const isFirst = monster.isfirst === 1;
                const nameStyle = isFirst ? 'color: var(--color-primary); font-weight: 600;' : 'color: var(--color-text-primary);';
                const firstBadge = isFirst ? '<span style="background: var(--color-primary); color: white; padding: 0 2px; border-radius: 2px; font-size: 7px; margin-right: 2px;">首</span>' : '';
                
                // 系别颜色
                const elemColor = {
                    '金': '#b8860b', '木': '#2e7d32', '水': '#1565c0', '火': '#d84315', '土': '#795548',
                    '妖': '#7b1fa2', '魔': '#4a148c', '毒': '#558b2f', '圣': '#b8860b', '翼': '#4f86c6',
                    '雷': )HTML" \
R"HTML('#c9a227', '幻': '#ab47bc', '怪': '#616161', '风': '#00838f', '灵': '#00897b'
                };
                const typeColor = elemColor[monster.typeName] || 'var(--color-text-primary)';
                
                // 简化资质名称显示（取前2字）
                const shortAptitude = aptitudeName.length > 2 ? aptitudeName.substring(0, 2) : aptitudeName;
                
                // 构建资质数值显示（顺序：体力/攻击/防御/法术/抗性/速度）
                // geniusList顺序：攻击[0]/防御[1]/法术[2]/抗性[3]/体力[4]/速度[5]
                let aptitudeDisplay = shortAptitude;
                if (monster.geniusList && monster.geniusList.length >= 6) {
                    const hp = monster.geniusList[4].value;      // 体力
                    const atk = monster.geniusList[0].value;     // 攻击
                    const def = monster.geniusList[1].value;     // 防御
                    const mag = monster.geniusList[2].value;     // 法术
                    const res = monster.geniusList[3].value;     // 抗性
                    const spd = mo)HTML" \
R"HTML(nster.geniusList[5].value;     // 速度
                    aptitudeDisplay = `${shortAptitude} 体${hp}/攻${atk}/防${def}/法${mag}/抗${res}/速${spd}`;
                }
                
                row.innerHTML = `
                    <span style="text-align: center; color: var(--color-text-tertiary); font-variant-numeric: tabular-nums;">${index + 1}</span>
                    <span style="text-align: center; ${nameStyle}" title="${monster.name || '未知'}">${firstBadge}${monster.name || '未知'}</span>
                    <span style="text-align: center; color: var(--color-text-tertiary); font-size: 8.5px;">${monster.iid || 0}</span>
                    <span style="text-align: center; color: ${sexColor};">${sexName}</span>
                    <span style="text-align: center;">${monster.level || 1}</span>
                    <span style="text-align: center; color: #d13438; font-variant-numeric: tabular-nums;">${monster.hp || 0}</span>
                    <span style="text-align: center; color: )HTML" \
R"HTML(${typeColor}; font-weight: 500;">${monster.typeName || '未知'}</span>
                    <span style="text-align: center; color: #8e44ad;">${geniusName}</span>
                    <span style="text-align: center; color: #c26a00; font-size: 8px;" title="${aptitudeName}">${aptitudeDisplay}</span>
                    <span style="color: var(--color-text-secondary); padding-left: 2px;" title="${skillNames}">${skillNames}</span>
                `;
                
                listContainer.appendChild(row);
            });
        };

        /**
         * 选择妖怪并显示详情
         */
        window.selectMonster = function(index) {
            window.selectedMonsterIndex = index;
            const monsters = window.currentMonsterData.monsters || [];
            const monster = monsters[index];
            
            if (!monster) return;
            
            // 重新渲染列表更新选中状态
            window.renderMonsterList(monsters);
            
            // 更新详情面板
            const detailContain)HTML" \
R"HTML(er = document.getElementById('monster-detail');
            if (!detailContainer) return;
            
            const geniusName = monster.geniusName || geniusNames[monster.geniusType] || '平衡';
            const aptitudeName = monster.aptitudeName || '未知';
            const isFirst = monster.isfirst === 1;
            
            // 获取性别
            const sexNames = ['未知', '雌', '雄', '无'];
            const sexName = sexNames[monster.sex] || '未知';
            
            // 构建技能列表
            let skillsHtml = '';
            if (monster.skills && monster.skills.length > 0) {
                monster.skills.forEach(skill => {
                    skillsHtml += `<div class="skill-row"><div>${skill.name || ('技能' + skill.id)}</div><div>${skill.pp || 0}/${skill.maxPp || 0}</div></div>`;
                });
            } else {
                skillsHtml = '<div class="flat-empty">无技能数据</div>';
            }
            
            // 构建灵玉列表
            let symmHtml = '';
            if ()HTML" \
R"HTML(monster.symmList && monster.symmList.length > 0) {
                monster.symmList.forEach(symm => {
                    symmHtml += `<span class="mb-chip">${symm.name || ('灵玉' + symm.id)}</span>`;
                });
            } else {
                symmHtml = '<span style="color: var(--color-text-tertiary);">无</span>';
            }
            
            const expPct = monster.needExp > 0 ? Math.min(100, Math.round((monster.exp || 0) * 100 / monster.needExp)) : 0;
            detailContainer.innerHTML = `
                <div style="display: flex; align-items: baseline; gap: 4px;">
                    <span style="font-size: 12px; font-weight: 600; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; color: ${isFirst ? 'var(--color-primary)' : 'var(--color-text-primary)'};">${monster.name || '未知'}</span>
                    ${isFirst ? '<span class="badge badge-success" style="padding: 0 4px; font-size: 8px;">首发</span>' : ''}
                    <span style="margin)HTML" \
R"HTML(-left: auto; flex: none; font-size: 8.5px; color: var(--color-text-tertiary);">Lv${monster.level || 1}</span>
                </div>
                <div style="display: flex; gap: 4px; font-size: 8.5px; color: var(--color-text-tertiary); white-space: nowrap; overflow: hidden;">
                    <span style="overflow: hidden; text-overflow: ellipsis;">${monster.typeName || '未知'} · ${sexName} · ${geniusName} · ${aptitudeName}</span>
                    <span style="margin-left: auto; flex: none; font-variant-numeric: tabular-nums;">经验 ${expPct}%</span>
                </div>
                <div style="margin-top: 1px; font-size: 8px; color: var(--color-text-tertiary); font-variant-numeric: tabular-nums; opacity: 0.85;">
                    ID ${monster.iid || 0} / ${monster.id || 0}
                </div>

                <div class="mb-sec">基础属性</div>
                <div class="mb-kv three">
                    <div><span class="bp-k">血量</span><span class="bp-v" style="color: #d13)HTML" \
R"HTML(438;">${monster.hp || 0}</span></div>
                    <div><span class="bp-k">攻击</span><span class="bp-v" style="color: #c26a00;">${monster.attack || 0}</span></div>
                    <div><span class="bp-k">防御</span><span class="bp-v" style="color: #2f6fb5;">${monster.defence || 0}</span></div>
                    <div><span class="bp-k">法术</span><span class="bp-v" style="color: #8e44ad;">${monster.magic || 0}</span></div>
                    <div><span class="bp-k">抗性</span><span class="bp-v" style="color: #12876f;">${monster.resistance || 0}</span></div>
                    <div><span class="bp-k">速度</span><span class="bp-v" style="color: #2e7d32;">${monster.speed || 0}</span></div>
                </div>


                <div class="mb-sec">资质 · ${aptitudeName}</div>
                <div class="mb-kv three">
                    ${(() => {
                        if (!monster.geniusList || monster.geniusList.length === 0) return '<div style="color: var(--color-text-tertiary);)HTML" \
R"HTML(">无资质数据</div>';
                        const starColors = ['', '#9e9e9e', '#4caf50', '#2196f3', '#9c27b0', '#ff9800'];
                        // geniusList顺序：攻击[0]/防御[1]/法术[2]/抗性[3]/体力[4]/速度[5]
                        // 显示顺序：体力/攻击/防御/法术/抗性/速度
                        const displayOrder = [4, 0, 1, 2, 3, 5];
                        return displayOrder.map(idx => {
                            const g = monster.geniusList[idx];
                            const level = g.level - 1; // 星级1-5（减1）
                            const stars = '★'.repeat(level) + '☆'.repeat(5 - level);
                            return `<div title="${g.name} Lv${g.level}">
                                <span class="bp-k">${g.name}</span>
                                <span class="bp-v" style="color: ${starColors[level] || 'var(--color-text-primary)'};">${g.value}</span>
                            </div>`;
                        }).join('');
                    })()}
                </div>

              )HTML" \
R"HTML(  <div class="mb-sec">技能 <span style="margin-left: auto;">${monster.skills ? monster.skills.length : 0} 个</span></div>
                ${skillsHtml}

                <div class="mb-sec">灵玉 <span style="margin-left: auto;">${monster.symmList ? monster.symmList.length : 0} 个</span></div>
                <div style="display: flex; flex-wrap: wrap;">${symmHtml}</div>
            `;        };

        /**
         * 刷新妖怪列表（发送封包请求）
         */
        window.queryMonsters = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'query_monsters'
                });
            }
        };
        
        // ============== 道具功能 ==============
        window.selectedItemId = 0;      // 当前选中的道具ID
        window.selectedItemName = '';   // 当前选中的道具名称
        window.selectedItemValue = 0;   // 当前选中的道具效果值
        window.itemCountMap = {};       // 道具数量映射表

        /**
         * 刷新背包物品
         */
       )HTML" \
R"HTML( window.refreshPackItems = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'refresh_pack_items'
                });
                window.updateHelperText('正在刷新背包...');
            }
        };

        /**
         * 更新道具数量显示
         * @param {Object} items - 道具数量对象 {itemId: count, ...}
         */
        window.updateItemCounts = function(items) {
            window.itemCountMap = items || {};
            // 更新每个按钮上的数量显示
            document.querySelectorAll('.item-count').forEach(el => {
                var itemId = el.getAttribute('data-id');
                var count = window.itemCountMap[itemId] || 0;
                el.textContent = '(' + count + ')';
                // 数量为0时变红提醒
                if (count === 0) {
                    el.style.color = '#e53935';
                } else {
                    el.style.color = '';
                }
            });
        };

     )HTML" \
R"HTML(   /**
         * 选择道具
         * @param {number} itemId - 道具ID
         * @param {string} itemName - 道具名称
         * @param {number} value - 道具效果值
         * @param {string} type - 道具类型 ('hp' 或 'mp')
         */
        window.selectItem = function(itemId, itemName, value, type) {
            window.selectedItemId = itemId;
            window.selectedItemName = itemName;
            window.selectedItemValue = value;
            window.selectedItemType = type || 'hp';

            // 更新按钮选中状态
            document.querySelectorAll('.item-btn').forEach(btn => btn.classList.remove('selected'));
            const currentBtn = document.querySelector('.item-btn[data-id="' + itemId + '"]');
            if (currentBtn) currentBtn.classList.add('selected');

            var typeText = type === 'mp' ? 'PP' : 'HP';
            var valueColor = type === 'mp' ? '#0078d4' : '#d13438';

            // 更新选中提示
            const hintEl = document.getElementById('selected-item-hint');
            if (hin)HTML" \
R"HTML(tEl) {
                hintEl.innerHTML = '<span style="color: var(--color-primary);">✓</span> ' + itemName +
                    ' <span style="color: ' + valueColor + ';">+' + value + typeText + '</span>';
            }

            window.updateHelperText('已选择: ' + itemName + ' (效果: +' + value + typeText + ')');
        };

        /**
         * 购买选中的道具
         */
        window.buySelectedItem = function() {
            if (!window.selectedItemId) {
                window.updateHelperText('请先选择要购买的道具');
                return;
            }
            
            const countInput = document.getElementById('item-count-input');
            const count = countInput ? parseInt(countInput.value) || 1 : 1;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'buy_item',
                    itemId: window.selectedItemId,
                    count: count
                });
                w)HTML" \
R"HTML(indow.updateHelperText('正在购买: ' + window.selectedItemName + ' x' + count);
            }
        };

        /**
         * 使用选中的道具（战斗中）
         */
        window.useSelectedItem = function() {
            if (!window.selectedItemId) {
                window.updateHelperText('请先选择要使用的道具');
                return;
            }
            
            const countInput = document.getElementById('item-count-input');
            const count = countInput ? parseInt(countInput.value) || 1 : 1;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'use_item',
                    itemId: window.selectedItemId,
                    count: count
                });
                window.updateHelperText('正在使用: ' + window.selectedItemName);
            }
        };
        
        // ============== BOSS专区功能 ==============
        window.bossList = [];  // 存储BOSS列表
        window.selectedBossId = 0;  )HTML" \
R"HTML(// 当前选中的BOSS ID

        /**
         * 初始化BOSS列表（从C++调用）
         * @param {Object|string} data - BOSS数据对象或JSON字符串
         */
        window.initBossList = function(data) {
            try {
                const bossData = typeof data === 'string' ? JSON.parse(data) : data;
                // 支持数组格式或对象格式
                window.bossList = Array.isArray(bossData) ? bossData : (bossData.bosses || []);
                window.renderBossSelect();
            } catch (e) {
                console.error('解析BOSS数据失败:', e);
            }
        };

        // 全局变量：BOSS列表缓存
        var cachedBossList = [];  // 缓存BOSS列表，避免重复遍历
        var isSelectingBoss = false;  // 防止选择时触发过滤

        /**
         * 渲染BOSS下拉选择框
         */
        window.renderBossSelect = function() {
            const select = document.getElementById('boss-select');
            if (!select) return;

            // 保存当前选中的值
            const currentValue = select.value;

            // 缓存BOSS列表
            cachedBossList = [)HTML" \
R"HTML(...window.bossList];

            select.innerHTML = '';

            // 添加默认选项
            const defaultOption = document.createElement('option');
            defaultOption.value = '0';
            defaultOption.textContent = '-- 请选择BOSS --';
            select.appendChild(defaultOption);

            // 添加BOSS选项
            window.bossList.forEach(boss => {
                const option = document.createElement('option');
                option.value = boss.id;
                option.textContent = `${boss.name} (ID: ${boss.id})`;
                select.appendChild(option);
            });

            // 恢复之前选中的值
            if (currentValue && select.querySelector(`option[value="${currentValue}"]`)) {
                select.value = currentValue;
            }
        };

        /**
         * 根据搜索框内容过滤BOSS选项
         */
        window.filterBossSelect = function() {
            // 如果正在选择BOSS，不执行过滤
            if (isSelectingBoss) return;

            const searchInput = document.get)HTML" \
R"HTML(ElementById('boss-search-input');
            const select = document.getElementById('boss-select');
            if (!searchInput || !select) return;

            const keyword = searchInput.value.toLowerCase().trim();
            const currentValue = select.value;

            // 清空选项
            select.innerHTML = '';

            // 添加默认选项
            const defaultOption = document.createElement('option');
            defaultOption.value = '0';
            defaultOption.textContent = '-- 请选择BOSS --';
            select.appendChild(defaultOption);

            // 过滤并添加BOSS选项
            const filteredBosses = keyword
                ? cachedBossList.filter(boss =>
                    boss.name.toLowerCase().includes(keyword) ||
                    boss.id.toString().includes(keyword)
                )
                : cachedBossList;

            filteredBosses.forEach(boss => {
                const option = document.createElement('option');
                option.value = boss.id;
)HTML" \
R"HTML(                option.textContent = `${boss.name} (ID: ${boss.id})`;
                select.appendChild(option);
            });

            // 尝试恢复之前选中的值
            if (currentValue && select.querySelector(`option[value="${currentValue}"]`)) {
                select.value = currentValue;
            }
        };

        /**
         * 选择BOSS
         */
        window.selectBoss = function(bossId) {
            const bossIdNum = parseInt(bossId, 10);
            window.selectedBossId = bossIdNum;

            const info = document.getElementById('selected-boss-info');

            if (bossIdNum === 0) {
                if (info) info.textContent = '请选择一个BOSS';
                return;
            }

            // 查找BOSS信息
            const boss = window.bossList.find(b => b.id === bossIdNum);
            if (boss) {
                if (info) {
                    info.innerHTML = `已选择: <span style="color: var(--color-primary); font-weight: 600;">${boss.name}</span> (ID: ${boss.id})HTML" \
R"HTML()`;
                }

                // 设置标志位，防止清空搜索框时触发过滤
                isSelectingBoss = true;

                // 清空搜索框，显示所有BOSS列表，避免选中的BOSS消失
                const searchInput = document.getElementById('boss-search-input');
                if (searchInput) {
                    searchInput.value = '';
                }

                // 重新渲染完整的BOSS列表
                const select = document.getElementById('boss-select');
                if (select) {
                    select.innerHTML = '';
                    
                    // 添加默认选项
                    const defaultOption = document.createElement('option');
                    defaultOption.value = '0';
                    defaultOption.textContent = '-- 请选择BOSS --';
                    select.appendChild(defaultOption);

                    // 添加所有BOSS选项
                    window.bossList.forEach(b => {
                        const option = document.createElement('option');
                        option.value = b)HTML" \
R"HTML(.id;
                        option.textContent = `${b.name} (ID: ${b.id})`;
                        select.appendChild(option);
                    });

                    // 设置选中的值
                    select.value = bossIdNum;
                }

                // 延迟重置标志位，确保所有操作完成
                setTimeout(function() {
                    isSelectingBoss = false;
                }, 100);
            }
        };

        /**
         * 进入BOSS战斗
         */
        window.enterBossBattle = function() {
            if (window.selectedBossId === 0) {
                if (window.updateHelperText) {
                    window.updateHelperText('请先选择一个BOSS');
                }
                return;
            }
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'enter_boss_battle',
                    bossId: window.selectedBossId
                });
                
                if (win)HTML" \
R"HTML(dow.updateHelperText) {
                    const boss = window.bossList.find(b => b.id === window.selectedBossId);
                    window.updateHelperText(`正在进入BOSS战斗: ${boss ? boss.name : window.selectedBossId}`);
                }
            }
        };

        /**
         * 显示更新对话框
         */
        window.showUpdateDialog = function(updateInfo) {
            const overlay = document.getElementById('update-dialog-overlay');
            const announcement = document.getElementById('update-dialog-announcement');
            const content = document.getElementById('update-dialog-content');
            const confirmBtn = document.getElementById('update-dialog-confirm');
            const cancelBtn = document.getElementById('update-dialog-cancel');

            if (!overlay || !announcement || !content || !confirmBtn || !cancelBtn) {
                console.error('Update dialog elements not found');
                return;
            }

            // 隐藏IE浏览框
            if ()HTML" \
R"HTML(window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'update-dialog-show' });
            }

            // 设置更新内容
            announcement.textContent = updateInfo.announcement || '';
            content.textContent = updateInfo.updateContent || '';

            // 绑定确定按钮事件 - 使用系统默认浏览器打开
            confirmBtn.onclick = function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'open-url',
                        url: updateInfo.downloadUrl
                    });
                }
                hideUpdateDialog();
            };

            // 绑定取消按钮事件
            cancelBtn.onclick = hideUpdateDialog;

            // 显示对话框
            overlay.classList.add('show');
        };

        /**
         * 隐藏更新对话框
         */
        function hideUpdateDialog() {
            const overlay = document.getElementById('update-dialog-overlay');
 )HTML" \
R"HTML(           if (overlay) {
                overlay.classList.remove('show');
            }
            // 恢复IE浏览框显示
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'update-dialog-hide' });
            }
        }

        /**
         * 显示 Key 登录对话框
         */
        function showKeyLoginDialog() {
            const overlay = document.getElementById('key-login-overlay');
            const input = document.getElementById('key-login-input');
            const confirmBtn = document.getElementById('key-login-confirm');
            const cancelBtn = document.getElementById('key-login-cancel');

            if (!overlay || !input || !confirmBtn || !cancelBtn) {
                console.error('Key login dialog elements not found');
                return;
            }

            // 隐藏IE浏览框
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'key-login-dialog-s)HTML" \
R"HTML(how' });
            }

            // 清空输入框
            input.value = '';

            // 绑定确定按钮事件
            confirmBtn.onclick = function() {
                const key = input.value.trim();
                if (key) {
                    if (window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage({
                            type: 'key-login',
                            key: key
                        });
                    }
                }
                hideKeyLoginDialog();
            };

            // 绑定取消按钮事件
            cancelBtn.onclick = hideKeyLoginDialog;

            // 显示对话框
            overlay.classList.add('show');

            // 聚焦输入框
            setTimeout(function() {
                input.focus();
            }, 100);
        }

        /**
         * 隐藏 Key 登录对话框
         */
        function hideKeyLoginDialog() {
            const overlay = document.getElementById('key-login-overlay');
            if (overlay)HTML" \
R"HTML() {
                overlay.classList.remove('show');
            }
            // 恢复IE浏览框显示
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'key-login-dialog-hide' });
            }
        }

        // 副本跳层功能
        function startDungeonJump() {
            const layerInput = document.getElementById('dungeon-layer-input');
            const targetLayer = layerInput ? parseInt(layerInput.value) || 1 : 1;
            
            if (targetLayer < 1 || targetLayer > 9999) {
                updateDungeonStatus('错误：层数必须在1-9999之间', 'error');
                return;
            }
            
            updateDungeonStatus('开始执行副本跳层到第' + targetLayer + '层...', 'info');
            
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'dungeon_jump_start',
                    targetLayer: targetLayer
   )HTML" \
R"HTML(             });
            }
        }
        
        function stopDungeonJump() {
            updateDungeonStatus('停止副本跳层', 'info');
            
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'dungeon_jump_stop'
                });
            }
        }
        
        function updateDungeonStatus(text, type) {
            const statusText = document.getElementById('dungeon-status-text');
            if (statusText) {
                statusText.textContent = text;
                
                // 根据类型设置颜色
                if (type === 'error') {
                    statusText.style.color = '#c62828';
                } else if (type === 'success') {
                    statusText.style.color = '#2e7d32';
                } else {
                    statusText.style.color = 'var(--color-text-secondary)';
                }
            }
        }
       )HTML" \
R"HTML( 
        // 接收C++发送的副本状态更新
        window.updateDungeonJumpStatus = function(text) {
            updateDungeonStatus(text, 'info');
        };

        // ============================================
        // 精魄赠送系统功能
        // ============================================
        
        // 精魄系统状态
        let spiritState = {
            dailyOut: 0,
            weeklyOut: 0,
            spiritList: [],
            selectedSpirit: null,
            lastRecordType: 0
        };
        let spiritPendingGift = null;

        function getSpiritDisplayName(spirit) {
            if (!spirit) {
                return '未知精魄';
            }
            const name = spirit.name || ('妖怪ID ' + spirit.eggIid);
            return '【' + name + '】精魄';
        }

        function getSpiritTypeName(type) {
            return type === 1 ? '普通' : type === 2 ? '稀有' : '特殊';
        }

        function getSpiritCharacterName(spirit) {
            if (!spirit) {
                return '未知性格';
           )HTML" \
R"HTML( }
            return spirit.characterName || (spirit.character > 0 ? ('性格' + spirit.character) : '未知性格');
        }

        function formatSpiritDate(ts) {
            if (!ts) {
                return '未知时间';
            }
            const date = new Date(ts * 1000);
            if (Number.isNaN(date.getTime())) {
                return '未知时间';
            }
            return date.getFullYear() + '-' +
                String(date.getMonth() + 1).padStart(2, '0') + '-' +
                String(date.getDate()).padStart(2, '0');
        }

        function getSpiritSkillSummary(spirit) {
            if (!spirit || !Array.isArray(spirit.skillNames) || !spirit.skillNames.length) {
                return '无技能';
            }
            return spirit.skillNames.slice(0, 3).join(' / ');
        }

        function getSpiritSelectedSummary(spirit) {
            if (!spirit) {
                return '无';
            }
            return getSpiritDisplayName(spirit) + '｜' +
                )HTML" \
R"HTML(getSpiritTypeName(spirit.eggType) + '｜' +
                (spirit.elemName || '未知系别') + '｜' +
                getSpiritCharacterName(spirit) + '｜' +
                formatSpiritDate(spirit.bornTime) + '｜' +
                getSpiritSkillSummary(spirit);
        }

        function getSpiritHistoryNameById(spiritId) {
            const matched = spiritState.spiritList.find(spirit => spirit.eggIid === spiritId);
            return matched && matched.name ? matched.name : ('ID:' + spiritId);
        }

        function escapeSpiritHtml(text) {
            return String(text == null ? '' : text)
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/"/g, '&quot;')
                .replace(/'/g, '&#39;');
        }

        function renderSpiritRecord(text, title) {
            const panel = document.getElementById('spirit-record-panel');
            const titleEl = document.getElementById('spirit-recor)HTML" \
R"HTML(d-title');
            if (titleEl) {
                titleEl.textContent = title || '记录面板';
            }
            if (panel) {
                panel.textContent = text || '暂无记录';
                panel.scrollTop = 0;
            }
        }

        function requestSpiritOpenUi() {
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'open_ui'
                });
            }
        }

        function requestSpiritList() {
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'getSpirits'
                });
            }
        }

        function refreshSpiritPanel() {
            updateSpiritStatus('正在刷新精魄数据...', 'info');
            requestSpiritOpenU)HTML" \
R"HTML(i();
            setTimeout(function() {
                requestSpiritList();
            }, 80);
        }
        
        // 获取精魄列表
        function getSpiritList() {
            updateSpiritStatus('正在获取精魄列表...', 'info');
            requestSpiritList();
        }
        
        // 显示精魄列表
        function renderSpiritList() {
            const container = document.getElementById('spirit-list');
            if (!container) return;

            if (!spiritState.spiritList.length) {
                spiritState.selectedSpirit = null;
                document.getElementById('spirit-selected').textContent = '无';
                container.innerHTML = '<div class="spirit-list-empty">暂无精魄</div>';
                return;
            }

            if (spiritState.selectedSpirit) {
                const selected = spiritState.spiritList.find(spirit => spirit.eggId === spiritState.selectedSpirit.eggId);
                spiritState.selectedSpirit = selected || null;
            }

            )HTML" \
R"HTML(if (spiritState.selectedSpirit) {
                document.getElementById('spirit-selected').textContent = getSpiritSelectedSummary(spiritState.selectedSpirit);
            }
            
            let html = '<div class="spirit-list-grid">';
            spiritState.spiritList.forEach(spirit => {
                const isSelected = spiritState.selectedSpirit && spiritState.selectedSpirit.eggId === spirit.eggId;
                html += '<div class="spirit-item' + (isSelected ? ' selected' : '') + '" onclick="selectSpirit(' + spirit.eggId + ')" title="' + escapeSpiritHtml(getSpiritSkillSummary(spirit)) + '">' +
                    '<div class="spirit-item-top">' +
                        '<span class="spirit-item-name">' + escapeSpiritHtml(getSpiritDisplayName(spirit)) + '</span>' +
                        '<span class="spirit-item-tag">#' + spirit.eggId + '</span>' +
                        '<div class="spirit-item-chips">' +
                            '<span class="spirit-mini-chip">)HTML" \
R"HTML(' + escapeSpiritHtml(getSpiritTypeName(spirit.eggType)) + '</span>' +
                            '<span class="spirit-mini-chip">' + escapeSpiritHtml(spirit.elemName || '未知系别') + '</span>' +
                            '<span class="spirit-mini-chip">' + escapeSpiritHtml(getSpiritCharacterName(spirit)) + '</span>' +
                        '</div>' +
                    '</div>' +
                '</div>';
            });
            html += '</div>';
            container.innerHTML = html;
        }
        
        // 选择精魄
        function selectSpirit(eggId) {
            const spirit = spiritState.spiritList.find(s => s.eggId === eggId);
            if (spirit) {
                spiritState.selectedSpirit = spirit;
                document.getElementById('spirit-selected').textContent = getSpiritSelectedSummary(spirit);
                renderSpiritList();
            }
        }
        
        // 验证并赠送精魄
        function verifyAndSendSpirit() {
            const friendIdInput = )HTML" \
R"HTML(document.getElementById('spirit-friend-id');
            const friendId = friendIdInput ? friendIdInput.value.trim() : '';
            
            if (!friendId) {
                updateSpiritStatus('请输入对方卡布号', 'error');
                return;
            }
            
            if (!/^\d+$/.test(friendId)) {
                updateSpiritStatus('卡布号只能包含数字', 'error');
                return;
            }
            
            if (!spiritState.selectedSpirit) {
                updateSpiritStatus('请先选择要赠送的精魄', 'error');
                return;
            }
            
            if (spiritState.dailyOut <= 0) {
                updateSpiritStatus('今日赠送次数已用完', 'error');
                return;
            }
            
            if (spiritState.weeklyOut <= 0) {
                updateSpiritStatus('本周赠送次数已用完', 'error');
                return;
            }
            
            // 先验证玩家
            updateSpiritStatus('正在验证玩家信息...', 'info');
            if (window.chrome && )HTML" \
R"HTML(window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'verifyPlayer',
                    friendId: parseInt(friendId)
                });
            }
        }
        
        // 获取历史记录
        function getSpiritRecord(type) {
            spiritState.lastRecordType = type;
            updateSpiritStatus('正在获取' + (type === 1 ? '赠送' : '接收') + '记录...', 'info');
            renderSpiritRecord('正在加载' + (type === 1 ? '赠送' : '接收') + '记录...', type === 1 ? '赠送记录' : '接收记录');
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'history',
                    recordType: type
                });
            }
        }
        
        // 更新精魄系统状态显示
        function updateSpiritStatus(text, type) {
    )HTML" \
R"HTML(        const statusEl = document.getElementById('spirit-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = type === 'error' ? '#c62828' : type === 'success' ? '#2e7d32' : 'var(--color-text-tertiary)';
            }
        }

        function hideSpiritConfirmDialog() {
            const overlay = document.getElementById('spirit-confirm-overlay');
            if (overlay) {
                overlay.classList.remove('show');
            }
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'spirit-confirm-dialog-hide' });
            }
            spiritPendingGift = null;
        }

        function showSpiritConfirmDialog(playerName, friendId) {
            const overlay = document.getElementById('spirit-confirm-overlay');
            const messageEl = document.getElementById('spirit-confirm-message');
            const confirmBtn = document.getElement)HTML" \
R"HTML(ById('spirit-confirm-accept');
            const cancelBtn = document.getElementById('spirit-confirm-cancel');

            if (!overlay || !messageEl || !confirmBtn || !cancelBtn || !spiritState.selectedSpirit) {
                updateSpiritStatus('确认弹窗初始化失败，请稍后重试', 'error');
                return;
            }

            spiritPendingGift = {
                friendId: friendId,
                playerName: playerName,
                eggId: spiritState.selectedSpirit.eggId,
                spiritName: getSpiritDisplayName(spiritState.selectedSpirit)
            };

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'spirit-confirm-dialog-show' });
            }

            messageEl.textContent = '确定要将【' + spiritPendingGift.spiritName + '】赠送给【' + playerName + '】吗？';

            cancelBtn.onclick = function() {
                hideSpiritConfirmDialog();
                updateSpiritStatus('已取消赠送', 'info');
          )HTML" \
R"HTML(  };
            overlay.onclick = function(event) {
                if (event.target === overlay) {
                    hideSpiritConfirmDialog();
                    updateSpiritStatus('已取消赠送', 'info');
                }
            };

            confirmBtn.onclick = function() {
                const pendingGift = spiritPendingGift;
                hideSpiritConfirmDialog();

                if (!pendingGift) {
                    updateSpiritStatus('赠送数据已失效，请重新验证玩家信息', 'error');
                    return;
                }

                if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                    window.chrome.webview.postMessage({
                        type: 'spiritCollect',
                        action: 'sendSpirit',
                        friendId: pendingGift.friendId,
                        eggId: pendingGift.eggId
                    });
                    updateSpiritStatus('正在发送精魄...', 'info');
                } else {
)HTML" \
R"HTML(                    updateSpiritStatus('当前环境不支持发送精魄请求', 'error');
                }
            };

            updateSpiritStatus('玩家验证成功，请确认赠送对象', 'info');
            overlay.classList.add('show');
        }
        
        // 接收C++发送的精魄系统数据
        window.handleSpiritCollectData = function(data) {
            switch(data.type) {
                case 'spiritState':
                    spiritState.dailyOut = data.data.dOut;
                    spiritState.weeklyOut = data.data.wOut;
                    document.getElementById('spirit-daily-out').textContent = data.data.dOut;
                    document.getElementById('spirit-weekly-out').textContent = data.data.wOut;
                    updateSpiritStatus('赠送状态已更新', 'success');
                    break;
                case 'spiritList':
                    spiritState.spiritList = data.data || [];
                    renderSpiritList();
                    updateSpiritStatus('已获取 ' + spiritState.spiritList.length + ' 个精魄', 'succe)HTML" \
R"HTML(ss');
                    break;
                case 'confirm':
                    const friendIdInput = document.getElementById('spirit-friend-id');
                    const friendId = friendIdInput ? parseInt(friendIdInput.value, 10) || 0 : 0;
                    if (!spiritState.selectedSpirit) {
                        updateSpiritStatus('当前没有选中的精魄，请重新选择后再试', 'error');
                        break;
                    }
                    if (friendId <= 0) {
                        updateSpiritStatus('卡布号无效，请重新输入后再试', 'error');
                        break;
                    }
                    showSpiritConfirmDialog(data.playerName, friendId);
                    break;
                case 'alert':
                    hideSpiritConfirmDialog();
                    updateSpiritStatus(data.message, 'error');
                    break;
                case 'history':
                    let recordText = data.recordType === 1 ? '赠送记录:\n' : '接收记录:\n';
                    i)HTML" \
R"HTML(f (data.json) {
                        try {
                            const recordList = JSON.parse(data.json);
                            if (Array.isArray(recordList) && recordList.length > 0) {
                                recordList.forEach(item => {
                                    if (Array.isArray(item) && item.length >= 5) {
                                        const date = new Date(item[3] * 1000);
                                        const dateText = date.getFullYear() + '年' + (date.getMonth() + 1) + '月' + date.getDate() + '日';
                                        const spiritName = getSpiritHistoryNameById(item[2]);
                                        if (item[4] === 2) {
                                            recordText += '【' + dateText + '】【' + item[1] + '】赠送你一个【' + spiritName + '精魄】\n';
                                        } else {
                                            recordText += '【' + dateText + '】你赠送给【' + item[1] + '】一个【' + spir)HTML" \
R"HTML(itName + '精魄】\n';
                                        }
                                    }
                                });
                            } else {
                                recordText += '暂无记录';
                            }
                        } catch (e) {
                            recordText += data.json;
                        }
                    } else {
                        recordText += '暂无记录';
                    }
                    renderSpiritRecord(recordText, data.recordType === 1 ? '赠送记录' : '接收记录');
                    updateSpiritStatus('记录已显示', 'success');
                    break;
                case 'sendSuccess':
                    hideSpiritConfirmDialog();
                    updateSpiritStatus(data.message || '精魄赠送成功！', 'success');
                    // 重新获取精魄列表
                    setTimeout(() => getSpiritList(), 500);
                    break;
            }
        };

        // 万妖盛会PVP功能
        document.addEven)HTML" \
R"HTML(tListener('DOMContentLoaded', function() {
            // 自动匹配按钮
            const autoMatchBtn = document.getElementById('battlesix-auto-match');
            if (autoMatchBtn) {
                autoMatchBtn.onclick = function() {
                    const matchCountInput = document.getElementById('battlesix-match-count');
                    const matchCount = matchCountInput ? parseInt(matchCountInput.value) || 1 : 1;
                    if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                        window.chrome.webview.postMessage({
                            type: 'battlesix_auto_match',
                            matchCount: matchCount
                        });
                    }
                };
            }

            // 取消匹配按钮
            const cancelMatchBtn = document.getElementById('battlesix-cancel-match');
            if (cancelMatchBtn) {
                cancelMatchBtn.onclick = function() {
                    if ()HTML" \
R"HTML(window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                        window.chrome.webview.postMessage({
                            type: 'battlesix_cancel_match'
                        });
                    }
                };
            }

            // 副本跳层按钮
            const dungeonJumpBtn = document.getElementById('dungeon-jump-btn');
            if (dungeonJumpBtn) {
                dungeonJumpBtn.onclick = function() {
                    startDungeonJump();
                };
            }
            
            // 副本停止按钮
            const dungeonStopBtn = document.getElementById('dungeon-stop-btn');
            if (dungeonStopBtn) {
                dungeonStopBtn.onclick = function() {
                    stopDungeonJump();
                };
            }

            renderSpiritRecord('点击右上角按钮查看赠送记录或接收记录。', '等待加载');
        });

    </script>
</body>
</html>
)HTML"
;

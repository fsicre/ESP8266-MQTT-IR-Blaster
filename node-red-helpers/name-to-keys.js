switch(msg.payload) {
    case "TF1"           : msg.payload = ["KEY1"]; break;
    case "FRANCE2"       : msg.payload = ["KEY2"]; break;
    case "FRANCE3"       : msg.payload = ["KEY3"]; break;
    case "CANAL+"        : msg.payload = ["KEY4"]; break;
    case "FRANCE5"       : msg.payload = ["KEY5"]; break;
    case "M6"            : msg.payload = ["KEY6"]; break;
    case "ARTE"          : msg.payload = ["KEY7"]; break;
    case "C8"            : msg.payload = ["KEY8"]; break;
    case "W9"            : msg.payload = ["KEY9"]; break;
    case "TMC"           : msg.payload = ["KEY1", "KEY0"]; break;
    case "TFX"           : msg.payload = ["KEY1", "KEY1"]; break;
    case "12"            : msg.payload = ["KEY1", "KEY2"]; break;
    case "13"            : msg.payload = ["KEY1", "KEY3"]; break;
    case "FRANCE4"       : msg.payload = ["KEY1", "KEY4"]; break;
    case "15"            : msg.payload = ["KEY1", "KEY5"]; break;
    case "16"            : msg.payload = ["KEY1", "KEY6"]; break;
    case "17"            : msg.payload = ["KEY1", "KEY7"]; break;
    case "GULLY"         : msg.payload = ["KEY1", "KEY8"]; break;
    case "FRANCEO"       : msg.payload = ["KEY1", "KEY9"]; break;
    case "20"            : msg.payload = ["KEY2", "KEY0"]; break;
    case "21"            : msg.payload = ["KEY2", "KEY1"]; break;
    case "22"            : msg.payload = ["KEY2", "KEY2"]; break;
    case "23"            : msg.payload = ["KEY2", "KEY3"]; break;
    case "RMCDECOUVERTE" : msg.payload = ["KEY2", "KEY4"]; break;
    case "HDMI1"         : msg.payload = ["SOURCE", "DOWN", "DOWN", "DOWN", "DOWN"]; break;
    case "HDMI2"         : msg.payload = ["SOURCE", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN"]; break;
    case "HDMI3"         : msg.payload = ["SOURCE", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN"]; break;
    default              : msg.payload = [ msg.payload ]; break;
}
return msg;

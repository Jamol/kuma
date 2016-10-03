
; (function () {
    var kuma = {},
        uidSeed = 0;
    kuma.allocUniqueId = function () {
        return ++uidSeed;
    }

    window.kuma = kuma;
})();
